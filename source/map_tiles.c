#include "map_tiles.h"
#include "network.h"
#include "simple_png.h"
#include "stb_image.h"
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
static unsigned char* rgba_linear_to_3ds_tiled(const unsigned char* src, int w, int h);
static C2D_Image create_image_from_decoded(const u8 *pixels);

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
    u8  *decoded_pixels;  /* Morton-tiled ABGR 256x256x4, NULL on failure */
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
static void      *dl_curl[DL_NUM_THREADS];  /* persistent curl handle per thread */

static void download_worker(void *arg) {
    int thread_id = (int)(size_t)arg;
    log_write("[THREAD %d] started\n", thread_id);
    /* One persistent curl handle per thread — reuses the TLS connection between
     * tile requests so we only pay the handshake cost on the first tile. */
    dl_curl[thread_id] = network_alloc_tile_curl();
    log_write("[THREAD %d] curl handle=%p\n", thread_id, dl_curl[thread_id]);
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

            /* Download raw tile bytes using persistent handle (no GPU ops here) */
            u8 *buffer = NULL;
            u32 size = 0;
            bool ok = download_tile_with_curl(req.tile_x, req.tile_y, req.zoom,
                                              dl_curl[thread_id], &buffer, &size);
            log_write("[THREAD %d] tile z=%d x=%d y=%d ok=%d size=%lu\n", thread_id, req.zoom, req.tile_x, req.tile_y, (int)ok, size);

            /* Decode on worker thread — JPEG decode + Morton conversion are the
             * bottleneck (~1-2 s on O3DS ARM11).  Only GPU ops need main thread. */
            u8 *decoded_pixels = NULL;
            if (ok && buffer && size >= 4) {
                bool is_jpeg = (buffer[0] == 0xFF && buffer[1] == 0xD8);
                int w = 0, h = 0;
                if (is_jpeg) {
                    int channels;
                    unsigned char *linear = stbi_load_from_memory(buffer, (int)size, &w, &h, &channels, 4);
                    if (linear) {
                        if (w == 256 && h == 256)
                            decoded_pixels = rgba_linear_to_3ds_tiled(linear, w, h);
                        else
                            log_write("[THREAD %d] unexpected JPEG size %dx%d\n", thread_id, w, h);
                        stbi_image_free(linear);
                    } else {
                        log_write("[THREAD %d] stbi JPEG decode failed: %s\n", thread_id, stbi_failure_reason());
                    }
                } else {
                    decoded_pixels = decode_png(buffer, (int)size, &w, &h);
                    if (decoded_pixels && (w != 256 || h != 256)) {
                        log_write("[THREAD %d] unexpected PNG size %dx%d\n", thread_id, w, h);
                        free(decoded_pixels);
                        decoded_pixels = NULL;
                    }
                }
                free(buffer);
                buffer = NULL;
            }

            /* Push result to main thread */
            LightLock_Lock(&dl_res_lock);
            if ((dl_res_tail - dl_res_head) < DL_QUEUE_SIZE) {
                dl_res_q[dl_res_tail & DL_QUEUE_MASK] = (DLResult){
                    .tile_x         = req.tile_x,
                    .tile_y         = req.tile_y,
                    .zoom           = req.zoom,
                    .slot           = req.slot,
                    .decoded_pixels = decoded_pixels,
                    .ok             = ok,
                };
                dl_res_tail++;
                decoded_pixels = NULL;   /* ownership transferred */
            }
            LightLock_Unlock(&dl_res_lock);

            if (buffer) free(buffer);              /* raw bytes if download failed */
            if (decoded_pixels) free(decoded_pixels); /* drop if result queue was full */
        }
    }
    /* Clean up the persistent curl handle when this thread exits */
    network_free_tile_curl(dl_curl[thread_id]);
    dl_curl[thread_id] = NULL;
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

    /* Drain result queue — free any decoded pixel buffers the thread produced */
    while (dl_res_head != dl_res_tail) {
        DLResult *r = &dl_res_q[dl_res_head & DL_QUEUE_MASK];
        if (r->decoded_pixels) { free(r->decoded_pixels); r->decoded_pixels = NULL; }
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

    /* Drain the result queue.  Tiles arrive pre-decoded (JPEG/PNG+Morton done
     * on the worker thread).  Only C3D_TexInit / C3D_TexUpload stay here. */
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
            if (res.ok && res.decoded_pixels) {
                C2D_Image img = create_image_from_decoded(res.decoded_pixels);
                if (img.tex) {
                    res.slot->image = img;
                    res.slot->valid = true;
                }
            }
            res.slot->pending = false;
        }
        if (res.decoded_pixels) free(res.decoded_pixels);
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
    
    /* Phase 1 — collect uncached visible tiles, sort by distance from the map
     * centre, then enqueue downloads centre-first so the tile under the
     * crosshair always enters the worker queue before edge/corner tiles. */
    int max_tile = 1 << zoom;
    typedef struct { int tile_x, tile_y, dist_sq; } PendingReq;
    PendingReq pq[16];
    int npq = 0;
    for (int ty = 0; ty < tiles_y; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            int tile_x = start_tile_x + tx;
            int tile_y = start_tile_y + ty;
            if (tile_x < 0 || tile_x >= max_tile || tile_y < 0 || tile_y >= max_tile) continue;
            if (!find_cached_tile(tile_x, tile_y, zoom) && npq < 16) {
                int dx = tile_x - center_tile_x;
                int dy = tile_y - center_tile_y;
                pq[npq].tile_x  = tile_x;
                pq[npq].tile_y  = tile_y;
                pq[npq].dist_sq = dx * dx + dy * dy;
                npq++;
            }
        }
    }
    /* Insertion sort — array is tiny (max ~9 tiles at zoom 10) */
    for (int i = 1; i < npq; i++) {
        PendingReq key = pq[i];
        int j = i - 1;
        while (j >= 0 && pq[j].dist_sq > key.dist_sq) { pq[j + 1] = pq[j]; j--; }
        pq[j + 1] = key;
    }
    for (int i = 0; i < npq; i++) {
        CachedTile *slot = get_cache_slot();
        if (slot) enqueue_tile_download(pq[i].tile_x, pq[i].tile_y, zoom, slot);
    }

    /* Phase 2 — render: draw cached tiles or grey placeholder */
    int tiles_rendered = 0;
    for (int ty = 0; ty < tiles_y; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            int tile_x = start_tile_x + tx;
            int tile_y = start_tile_y + ty;
            if (tile_x < 0 || tile_x >= max_tile || tile_y < 0 || tile_y >= max_tile) continue;

            CachedTile *cached = find_cached_tile(tile_x, tile_y, zoom);

            // Calculate render position
            int render_x = (tx * TILE_SIZE) - tile_offset_x + offset_x;
            int render_y = (ty * TILE_SIZE) - tile_offset_y + offset_y;

            // Only render if tile is visible on screen
            if (render_x + TILE_SIZE >= 0 && render_x < screen_width &&
                render_y + TILE_SIZE >= 0 && render_y < screen_height) {

                if (cached && cached->valid && cached->image.tex) {
                    cached->last_used = cache_counter++;
                    C2D_DrawImageAt(cached->image, render_x, render_y, 0.0f, NULL, 1.0f, 1.0f);
                    tiles_rendered++;
                } else {
                    // Draw placeholder for missing / in-flight tile
                    C2D_DrawRectSolid(render_x, render_y, 0.0f, TILE_SIZE, TILE_SIZE,
                                    C2D_Color32f(0.3f, 0.3f, 0.3f, 1.0f));
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

/* Convert a linear RGBA (4 bpp) buffer to the 3DS Morton-tiled ABGR layout
 * expected by C3D_TexUpload.  Returns a newly malloc'd buffer; caller frees it.
 * width and height must both be multiples of 8. */
static unsigned char* rgba_linear_to_3ds_tiled(const unsigned char* src, int w, int h) {
    unsigned char* tiled = (unsigned char*)malloc(w * h * 4);
    if (!tiled) return NULL;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int src_idx = (y * w + x) * 4;

            int tile_x    = x >> 3;
            int tile_y    = y >> 3;
            int in_tile_x = x & 7;
            int in_tile_y = y & 7;

            int morton =
                ((in_tile_x & 1) << 0) | ((in_tile_y & 1) << 1) |
                ((in_tile_x & 2) << 1) | ((in_tile_y & 2) << 2) |
                ((in_tile_x & 4) << 2) | ((in_tile_y & 4) << 3);

            int tiles_per_row = w >> 3;
            int tile_offset   = (tile_y * tiles_per_row + tile_x) * 64;
            int dst_idx       = (tile_offset + morton) * 4;

            /* GPU_RGBA8 on 3DS is stored A, B, G, R in memory */
            tiled[dst_idx + 0] = src[src_idx + 3]; /* A */
            tiled[dst_idx + 1] = src[src_idx + 2]; /* B */
            tiled[dst_idx + 2] = src[src_idx + 1]; /* G */
            tiled[dst_idx + 3] = src[src_idx + 0]; /* R */
        }
    }
    return tiled;
}

/* GPU-only texture creation from a pre-decoded Morton-tiled ABGR buffer.
 * Called on the main thread; pixels were decoded by the worker thread.
 * Does NOT free pixels — caller is responsible. */
static C2D_Image create_image_from_decoded(const u8 *pixels) {
    C2D_Image image = {0};

    C3D_Tex *tex = (C3D_Tex *)malloc(sizeof(C3D_Tex));
    if (!tex) return image;

    if (!C3D_TexInit(tex, 256, 256, GPU_RGBA8)) {
        free(tex);
        return image;
    }

    C3D_TexUpload(tex, pixels);
    C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    Tex3DS_SubTexture *subtex = (Tex3DS_SubTexture *)malloc(sizeof(Tex3DS_SubTexture));
    if (!subtex) {
        C3D_TexDelete(tex);
        free(tex);
        return image;
    }

    subtex->width  = 256;
    subtex->height = 256;
    subtex->left   = 0.0f;
    subtex->top    = 1.0f;
    subtex->right  = 1.0f;
    subtex->bottom = 0.0f;

    image.tex    = tex;
    image.subtex = subtex;
    return image;
}
