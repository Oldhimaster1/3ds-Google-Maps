#include "map_tiles.h"
#include "network.h"
#include "simple_png.h"
#include "logging.h"
#include <citro2d.h>
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_CACHED_TILES 64
#define TILE_SIZE 256

typedef struct {
    int tile_x;
    int tile_y;
    int zoom;
    C2D_Image image;
    bool valid;
    bool pending;   /* download in-flight — do not evict or re-request */
    u32 last_used;
} CachedTile;

static CachedTile tile_cache[MAX_CACHED_TILES];
static u32 cache_counter = 0;
static bool initialized = false;

typedef struct {
    int tile_x;
    int tile_y;
    int zoom;
} TileCoord;

#define PREFETCH_MAX_QUEUE 256
static TileCoord prefetch_queue[PREFETCH_MAX_QUEUE];
static int prefetch_count = 0;
static int prefetch_index = 0;
static bool prefetch_active = false;
static int prefetch_zoom = -1;
static int prefetch_start_x = 0;
static int prefetch_start_y = 0;
static int prefetch_tiles_x = 0;
static int prefetch_tiles_y = 0;
static int prefetch_ring = 0;

static void free_cached_tile(CachedTile* tile) {
    if (!tile) return;

    if (tile->image.tex) {
        C3D_TexDelete(tile->image.tex);
        free(tile->image.tex);
        tile->image.tex = NULL;
    }
    if (tile->image.subtex) {
        free((void*)tile->image.subtex);
        tile->image.subtex = NULL;
    }

    tile->valid = false;
    tile->tile_x = 0;
    tile->tile_y = 0;
    tile->zoom = 0;
    tile->last_used = 0;
}

void map_tiles_prefetch_reset(void) {
    prefetch_count = 0;
    prefetch_index = 0;
    prefetch_active = false;
    prefetch_zoom = -1;
    prefetch_start_x = 0;
    prefetch_start_y = 0;
    prefetch_tiles_x = 0;
    prefetch_tiles_y = 0;
    prefetch_ring = 0;
}

// Private function prototypes
static CachedTile* find_cached_tile(int tile_x, int tile_y, int zoom);
static CachedTile* get_cache_slot(void);

/* -------------------------------------------------------------------------
 * Background download thread
 *
 * The worker thread downloads raw tile bytes only. GPU operations (C3D_Tex*)
 * must happen on the main thread, so map_tiles_process_downloads() is called
 * once per frame from main.c to do the decode + texture upload.
 * -------------------------------------------------------------------------*/
typedef struct {
    int tile_x, tile_y, zoom;
    CachedTile *slot;
} DLRequest;

typedef struct {
    int tile_x, tile_y, zoom;
    CachedTile *slot;
    u8  *buffer;
    u32  size;
    bool ok;
} DLResult;

#define DL_QUEUE_SIZE 32
#define DL_QUEUE_MASK (DL_QUEUE_SIZE - 1)

static DLRequest  dl_req_q[DL_QUEUE_SIZE];
static int        dl_req_head = 0, dl_req_tail = 0;
static LightLock  dl_req_lock;

static DLResult   dl_res_q[DL_QUEUE_SIZE];
static int        dl_res_head = 0, dl_res_tail = 0;
static LightLock  dl_res_lock;

static LightEvent dl_work_event;
#define DL_NUM_THREADS 2
static Thread     dl_thread_handles[DL_NUM_THREADS];
static volatile bool dl_thread_running = false;

static void download_worker(void *arg) {
    int thread_id = (int)(size_t)arg;
    log_write("[THREAD %d] started\n", thread_id);
    (void)arg;
    while (dl_thread_running) {
        LightEvent_Wait(&dl_work_event);
        log_write("[THREAD %d] woke up\n", thread_id);
        while (dl_thread_running) {
            /* Dequeue one request */
            LightLock_Lock(&dl_req_lock);
            if (dl_req_head == dl_req_tail) {
                LightLock_Unlock(&dl_req_lock);
                break;
            }
            DLRequest req = dl_req_q[dl_req_head & DL_QUEUE_MASK];
            dl_req_head++;
            LightLock_Unlock(&dl_req_lock);
            log_write("[THREAD %d] dequeued tile z=%d x=%d y=%d\n", thread_id, req.zoom, req.tile_x, req.tile_y);

            /* Download raw tile bytes (no GPU ops here) */
            u8 *buffer = NULL;
            u32 size = 0;
            bool ok = download_tile(req.tile_x, req.tile_y, req.zoom, &buffer, &size);
            log_write("[THREAD %d] tile z=%d x=%d y=%d ok=%d size=%lu\n", thread_id, req.zoom, req.tile_x, req.tile_y, (int)ok, size);

            /* Push result to main thread */
            LightLock_Lock(&dl_res_lock);
            if ((dl_res_tail - dl_res_head) < DL_QUEUE_SIZE) {
                dl_res_q[dl_res_tail & DL_QUEUE_MASK] = (DLResult){
                    .tile_x = req.tile_x,
                    .tile_y = req.tile_y,
                    .zoom   = req.zoom,
                    .slot   = req.slot,
                    .buffer = buffer,
                    .size   = size,
                    .ok     = ok,
                };
                dl_res_tail++;
                buffer = NULL;   /* ownership transferred */
            }
            LightLock_Unlock(&dl_res_lock);

            if (buffer) free(buffer);  /* drop if result queue was full */
        }
    }
}

static void enqueue_tile_download(int tile_x, int tile_y, int zoom, CachedTile *slot) {
    /* is_connected() uses ACU which may not work from background threads.
     * Check it here on the main thread before enqueuing. */
    if (!is_connected()) {
        log_write("[TILES] enqueue skipped (not connected) z=%d x=%d y=%d\n", zoom, tile_x, tile_y);
        return;
    }
    log_write("[TILES] enqueue z=%d x=%d y=%d\n", zoom, tile_x, tile_y);

    slot->pending = true;
    slot->valid   = false;
    slot->tile_x  = tile_x;
    slot->tile_y  = tile_y;
    slot->zoom    = zoom;
    slot->last_used = cache_counter++;

    LightLock_Lock(&dl_req_lock);
    if ((dl_req_tail - dl_req_head) < DL_QUEUE_SIZE) {
        dl_req_q[dl_req_tail & DL_QUEUE_MASK] = (DLRequest){
            .tile_x = tile_x,
            .tile_y = tile_y,
            .zoom   = zoom,
            .slot   = slot,
        };
        dl_req_tail++;
        LightLock_Unlock(&dl_req_lock);
        LightEvent_Signal(&dl_work_event);
    } else {
        /* Queue full — clear pending so the slot stays reclaimable */
        slot->pending = false;
        LightLock_Unlock(&dl_req_lock);
    }
}
static C2D_Image create_image_from_buffer(u8* buffer, u32 size);

bool map_tiles_init(void) {
    if (initialized) return true;

    memset(tile_cache, 0, sizeof(tile_cache));

    /* Initialise download thread infrastructure */
    LightLock_Init(&dl_req_lock);
    LightLock_Init(&dl_res_lock);
    LightEvent_Init(&dl_work_event, RESET_ONESHOT);
    dl_req_head = dl_req_tail = 0;
    dl_res_head = dl_res_tail = 0;
    dl_thread_running = true;
    /* Two worker threads — if one is stuck on a slow HTTPS handshake the
     * other can still service queued requests. 128 KB stack each. */
    for (int i = 0; i < DL_NUM_THREADS; i++) {
        dl_thread_handles[i] = threadCreate(download_worker, (void*)(size_t)i, 128 * 1024, 0x3C, -2, false);
        log_write("[TILES] started download thread %d handle=%p\n", i, (void*)dl_thread_handles[i]);
    }

    initialized = true;
    return true;
}

u32 map_tiles_get_cache_used(void) {
    u32 used = 0;
    for (int i = 0; i < MAX_CACHED_TILES; i++) {
        if (tile_cache[i].valid) used++;
    }
    return used;
}

u32 map_tiles_get_cache_capacity(void) {
    return MAX_CACHED_TILES;
}

void map_tiles_cleanup(void) {
    if (!initialized) return;

    /* Stop the background download threads */
    dl_thread_running = false;
    LightEvent_Signal(&dl_work_event);  /* wake all threads so they can exit */
    for (int i = 0; i < DL_NUM_THREADS; i++) {
        if (dl_thread_handles[i]) {
            threadJoin(dl_thread_handles[i], 5000000000ULL);  /* 5s max wait */
            threadFree(dl_thread_handles[i]);
            dl_thread_handles[i] = NULL;
        }
    }

    /* Drain result queue — free any buffers the thread produced */
    while (dl_res_head != dl_res_tail) {
        DLResult *r = &dl_res_q[dl_res_head & DL_QUEUE_MASK];
        if (r->buffer) { free(r->buffer); r->buffer = NULL; }
        dl_res_head++;
    }

    /* Free all cached tile images */
    for (int i = 0; i < MAX_CACHED_TILES; i++) {
        free_cached_tile(&tile_cache[i]);
    }

    map_tiles_prefetch_reset();
    initialized = false;
}

void map_tiles_process_downloads(void) {
    if (!initialized) return;

    /* Drain the result queue.  GPU ops (C3D_TexInit / C3D_TexUpload) must run
     * on the main thread, so we do PNG decode + texture upload here. */
    while (dl_res_head != dl_res_tail) {
        LightLock_Lock(&dl_res_lock);
        DLResult res = dl_res_q[dl_res_head & DL_QUEUE_MASK];
        dl_res_head++;
        LightLock_Unlock(&dl_res_lock);

        /* Only write to the slot if it is still waiting for this exact tile */
        if (res.slot->pending &&
            res.slot->tile_x == res.tile_x &&
            res.slot->tile_y == res.tile_y &&
            res.slot->zoom   == res.zoom) {
            if (res.ok && res.buffer && res.size >= 8 &&
                res.buffer[0] == 0x89 && res.buffer[1] == 0x50 &&
                res.buffer[2] == 0x4E && res.buffer[3] == 0x47) {
                C2D_Image img = create_image_from_buffer(res.buffer, res.size);
                if (img.tex) {
                    res.slot->image = img;
                    res.slot->valid = true;
                }
            }
            res.slot->pending = false;
        }
        if (res.buffer) free(res.buffer);
    }
}

void map_tiles_render(double lat, double lon, int zoom, int offset_x, int offset_y, 
                     int screen_width, int screen_height, bool is_top_screen) {
    if (!initialized) return;
    
    // Calculate the tile coordinates for the center of the screen
    int center_tile_x, center_tile_y;
    lat_lon_to_tile(lat, lon, zoom, &center_tile_x, &center_tile_y);
    
    // Calculate how many tiles we need to cover the screen
    int tiles_x = (screen_width / TILE_SIZE) + 2;
    int tiles_y = (screen_height / TILE_SIZE) + 2;
    
    // Calculate starting tile position
    int start_tile_x = center_tile_x - tiles_x / 2;
    int start_tile_y = center_tile_y - tiles_y / 2;
    
    // Calculate pixel offset within the center tile
    int center_pixel_x, center_pixel_y;
    lat_lon_to_pixel(lat, lon, zoom, &center_pixel_x, &center_pixel_y);
    int tile_offset_x = center_pixel_x % TILE_SIZE;
    int tile_offset_y = center_pixel_y % TILE_SIZE;
    
    // Render tiles
    int tiles_rendered = 0;
    for (int ty = 0; ty < tiles_y; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            int tile_x = start_tile_x + tx;
            int tile_y = start_tile_y + ty;
            
            // Skip invalid tile coordinates
            int max_tile = 1 << zoom;
            if (tile_x < 0 || tile_x >= max_tile || tile_y < 0 || tile_y >= max_tile) {
                continue;
            }
            
            // Try to find the tile in cache
            CachedTile* cached = find_cached_tile(tile_x, tile_y, zoom);
            if (!cached) {
                // Tile not in cache — queue a background download (non-blocking)
                cached = get_cache_slot();
                if (cached) {
                    enqueue_tile_download(tile_x, tile_y, zoom, cached);
                }
            }
            
            // Calculate render position
            int render_x = (tx * TILE_SIZE) - tile_offset_x + offset_x;
            int render_y = (ty * TILE_SIZE) - tile_offset_y + offset_y;
            
            // Only render if tile is visible on screen
            if (render_x + TILE_SIZE >= 0 && render_x < screen_width &&
                render_y + TILE_SIZE >= 0 && render_y < screen_height) {
                
                if (cached && cached->valid && cached->image.tex) {
                    // Update last used time
                    cached->last_used = cache_counter++;
                    // Draw the decoded PNG tile
                    C2D_DrawImageAt(cached->image, render_x, render_y, 0.0f, NULL, 1.0f, 1.0f);
                    tiles_rendered++;
                } else {
                    // Draw placeholder for missing tile (dark gray with X pattern)
                    C2D_DrawRectSolid(render_x, render_y, 0.0f, TILE_SIZE, TILE_SIZE,
                                    C2D_Color32f(0.3f, 0.3f, 0.3f, 1.0f));
                    
                    // Draw X to indicate missing tile
                    C2D_DrawRectSolid(render_x + 10, render_y + 10, 0.1f, TILE_SIZE - 20, 4,
                                    C2D_Color32f(1.0f, 0.0f, 0.0f, 0.8f));
                    C2D_DrawRectSolid(render_x + 10, render_y + TILE_SIZE - 14, 0.1f, TILE_SIZE - 20, 4,
                                    C2D_Color32f(1.0f, 0.0f, 0.0f, 0.8f));
                }
            }
        }
    }
    
    // Draw crosshair at center (top screen only)
    if (is_top_screen) {
        int center_x = screen_width / 2;
        int center_y = screen_height / 2;
        
        // Draw crosshair lines
        C2D_DrawRectSolid(center_x - 10, center_y - 1, 0.5f, 20, 2,
                         C2D_Color32f(1.0f, 0.0f, 0.0f, 0.8f));
        C2D_DrawRectSolid(center_x - 1, center_y - 10, 0.5f, 2, 20,
                         C2D_Color32f(1.0f, 0.0f, 0.0f, 0.8f));
    }
    
}

void map_tiles_clear_cache(void) {
    if (!initialized) return;
    
    // Free all cached tiles
    for (int i = 0; i < MAX_CACHED_TILES; i++) {
        free_cached_tile(&tile_cache[i]);
    }

    map_tiles_prefetch_reset();
}

static void build_prefetch_queue(double lat, double lon, int zoom, int screen_width, int screen_height, int ring) {
    // Match the render grid (inner rect) and prefetch just outside it.
    int center_tile_x, center_tile_y;
    lat_lon_to_tile(lat, lon, zoom, &center_tile_x, &center_tile_y);

    int tiles_x = (screen_width / TILE_SIZE) + 2;
    int tiles_y = (screen_height / TILE_SIZE) + 2;
    int start_tile_x = center_tile_x - tiles_x / 2;
    int start_tile_y = center_tile_y - tiles_y / 2;

    prefetch_count = 0;
    prefetch_index = 0;
    prefetch_active = true;
    prefetch_zoom = zoom;
    prefetch_start_x = start_tile_x;
    prefetch_start_y = start_tile_y;
    prefetch_tiles_x = tiles_x;
    prefetch_tiles_y = tiles_y;
    prefetch_ring = ring;

    int inner_x0 = start_tile_x;
    int inner_y0 = start_tile_y;
    int inner_x1 = start_tile_x + tiles_x - 1;
    int inner_y1 = start_tile_y + tiles_y - 1;

    int outer_x0 = inner_x0 - ring;
    int outer_y0 = inner_y0 - ring;
    int outer_x1 = inner_x1 + ring;
    int outer_y1 = inner_y1 + ring;

    int max_tile = 1 << zoom;

    for (int ty = outer_y0; ty <= outer_y1; ty++) {
        for (int tx = outer_x0; tx <= outer_x1; tx++) {
            // Skip inner rect (those tiles are handled by render).
            if (tx >= inner_x0 && tx <= inner_x1 && ty >= inner_y0 && ty <= inner_y1) continue;
            if (tx < 0 || tx >= max_tile || ty < 0 || ty >= max_tile) continue;
            if (find_cached_tile(tx, ty, zoom)) continue;
            if (prefetch_count >= PREFETCH_MAX_QUEUE) return;
            prefetch_queue[prefetch_count++] = (TileCoord){ .tile_x = tx, .tile_y = ty, .zoom = zoom };
        }
    }
}

void map_tiles_prefetch_step(double lat, double lon, int zoom, int screen_width, int screen_height, int ring) {
    if (!initialized) return;
    if (ring <= 0) return;

    // Rebuild queue if view changed materially.
    int center_tile_x, center_tile_y;
    lat_lon_to_tile(lat, lon, zoom, &center_tile_x, &center_tile_y);

    int tiles_x = (screen_width / TILE_SIZE) + 2;
    int tiles_y = (screen_height / TILE_SIZE) + 2;
    int start_tile_x = center_tile_x - tiles_x / 2;
    int start_tile_y = center_tile_y - tiles_y / 2;

    bool view_changed = !prefetch_active ||
                        prefetch_zoom != zoom ||
                        prefetch_start_x != start_tile_x ||
                        prefetch_start_y != start_tile_y ||
                        prefetch_tiles_x != tiles_x ||
                        prefetch_tiles_y != tiles_y ||
                        prefetch_ring != ring;

    if (view_changed) {
        build_prefetch_queue(lat, lon, zoom, screen_width, screen_height, ring);
    }

    // Throttle: load at most one tile per call.
    while (prefetch_index < prefetch_count) {
        TileCoord c = prefetch_queue[prefetch_index++];
        if (find_cached_tile(c.tile_x, c.tile_y, c.zoom)) {
            continue;
        }

        CachedTile* slot = get_cache_slot();
        if (!slot) return;
        enqueue_tile_download(c.tile_x, c.tile_y, c.zoom, slot);
        return;
    }

    // Queue finished.
    prefetch_active = false;
}

void lat_lon_to_tile(double lat, double lon, int zoom, int* tile_x, int* tile_y) {
    double lat_rad = lat * M_PI / 180.0;
    int n = 1 << zoom;
    
    *tile_x = (int)((lon + 180.0) / 360.0 * n);
    *tile_y = (int)((1.0 - asinh(tan(lat_rad)) / M_PI) / 2.0 * n);
}

void tile_to_lat_lon(int tile_x, int tile_y, int zoom, double* lat, double* lon) {
    int n = 1 << zoom;
    
    *lon = tile_x / (double)n * 360.0 - 180.0;
    double lat_rad = atan(sinh(M_PI * (1 - 2 * tile_y / (double)n)));
    *lat = lat_rad * 180.0 / M_PI;
}

void lat_lon_to_pixel(double lat, double lon, int zoom, int* pixel_x, int* pixel_y) {
    double lat_rad = lat * M_PI / 180.0;
    int n = 1 << zoom;
    
    double x = (lon + 180.0) / 360.0 * n;
    double y = (1.0 - asinh(tan(lat_rad)) / M_PI) / 2.0 * n;
    
    *pixel_x = (int)(x * TILE_SIZE);
    *pixel_y = (int)(y * TILE_SIZE);
}

static CachedTile* find_cached_tile(int tile_x, int tile_y, int zoom) {
    for (int i = 0; i < MAX_CACHED_TILES; i++) {
        if ((tile_cache[i].valid || tile_cache[i].pending) &&
            tile_cache[i].tile_x == tile_x &&
            tile_cache[i].tile_y == tile_y &&
            tile_cache[i].zoom == zoom) {
            return &tile_cache[i];
        }
    }
    return NULL;
}

static CachedTile* get_cache_slot(void) {
    // First, try to find a free slot (not valid AND not pending)
    for (int i = 0; i < MAX_CACHED_TILES; i++) {
        if (!tile_cache[i].valid && !tile_cache[i].pending) {
            free_cached_tile(&tile_cache[i]);
            return &tile_cache[i];
        }
    }

    // Evict the oldest non-pending slot
    u32 oldest_time = UINT32_MAX;
    int oldest_index = -1;
    for (int i = 0; i < MAX_CACHED_TILES; i++) {
        if (!tile_cache[i].pending && tile_cache[i].last_used < oldest_time) {
            oldest_time = tile_cache[i].last_used;
            oldest_index = i;
        }
    }

    if (oldest_index < 0) return NULL;  // all slots in-flight

    free_cached_tile(&tile_cache[oldest_index]);
    return &tile_cache[oldest_index];
}

static C2D_Image create_image_from_buffer(u8* buffer, u32 size) {
    C2D_Image image = {0};
    int width, height;
    unsigned char* pixels = decode_png(buffer, (int)size, &width, &height);

    if (!pixels) {
        return image;
    }

    if (width != 256 || height != 256) {
        free_png_data(pixels);
        return image;
    }
    
    // Create C3D texture
    C3D_Tex* tex = (C3D_Tex*)malloc(sizeof(C3D_Tex));
    if (!tex) {
        free_png_data(pixels);
        return image;
    }

    // Initialize RGBA8 texture
    if (!C3D_TexInit(tex, width, height, GPU_RGBA8)) {
        free(tex);
        free_png_data(pixels);
        return image;
    }

    // Upload tiled RGBA pixel data
    C3D_TexUpload(tex, pixels);

    // Set texture parameters
    C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    
    // Create subtexture descriptor
    Tex3DS_SubTexture* subtex = (Tex3DS_SubTexture*)malloc(sizeof(Tex3DS_SubTexture));
    if (!subtex) {
        C3D_TexDelete(tex);
        free(tex);
        free_png_data(pixels);
        return image;
    }
    
    // Set up subtexture coordinates
    subtex->width = width;
    subtex->height = height;
    subtex->left = 0.0f;
    subtex->top = 1.0f;
    subtex->right = 1.0f;
    subtex->bottom = 0.0f;
    
    // Set up the C2D_Image
    image.tex = tex;
    image.subtex = subtex;

    free_png_data(pixels);
    return image;
}
