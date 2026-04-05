#include "map_tiles.h"
#include "network.h"
#include "simple_png.h"
#include <citro2d.h>
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
    u32 last_used;
} CachedTile;

static CachedTile tile_cache[MAX_CACHED_TILES];
static u32 cache_counter = 0;
static bool initialized = false;

// Private function prototypes
static CachedTile* find_cached_tile(int tile_x, int tile_y, int zoom);
static CachedTile* get_cache_slot(void);
static bool load_tile_from_network(int tile_x, int tile_y, int zoom, CachedTile* tile);
static C2D_Image create_image_from_buffer(u8* buffer, u32 size);

bool map_tiles_init(void) {
    if (initialized) return true;
    
    // Initialize tile cache
    memset(tile_cache, 0, sizeof(tile_cache));
    
    initialized = true;
    return true;
}

void map_tiles_cleanup(void) {
    if (!initialized) return;
    
    // Free all cached tile images
    for (int i = 0; i < MAX_CACHED_TILES; i++) {
        if (tile_cache[i].valid) {
            // Note: In a real implementation, you'd free the texture memory here
            tile_cache[i].valid = false;
        }
    }
    
    initialized = false;
}

void map_tiles_render(double lat, double lon, int zoom, int offset_x, int offset_y, 
                     int screen_width, int screen_height, bool is_top_screen) {
    if (!initialized) return;
    
    // Calculate the world pixel coordinates for the center lat/lon
    int center_world_x, center_world_y;
    lat_lon_to_pixel(lat, lon, zoom, &center_world_x, &center_world_y);
    
    // The center of the screen should show center_world_x/y
    // So the top-left of the screen shows:
    int screen_world_x = center_world_x - screen_width / 2;
    int screen_world_y = center_world_y - screen_height / 2;
    
    // Calculate which tiles we need
    int start_tile_x = screen_world_x / TILE_SIZE;
    int start_tile_y = screen_world_y / TILE_SIZE;
    int end_tile_x = (screen_world_x + screen_width) / TILE_SIZE + 1;
    int end_tile_y = (screen_world_y + screen_height) / TILE_SIZE + 1;
    
    // Handle negative world coordinates (shouldn't happen, but be safe)
    if (screen_world_x < 0) start_tile_x--;
    if (screen_world_y < 0) start_tile_y--;
    
    int tiles_x = end_tile_x - start_tile_x + 1;
    int tiles_y = end_tile_y - start_tile_y + 1;
    
    static int debug_counter = 0;
    debug_counter++;
    
    // Debug: Print tile info occasionally
    if (debug_counter % 60 == 0 && is_top_screen) {
        printf("Rendering tiles at zoom %d, start: %d,%d\n", zoom, start_tile_x, start_tile_y);
    }
    
    // Center tile — used to sort uncached tiles by distance so the middle loads first
    int center_tile_x = center_world_x / TILE_SIZE;
    int center_tile_y = center_world_y / TILE_SIZE;
    int max_tile = 1 << zoom;

    // Pending tile queue: uncached visible tiles sorted closest-to-center first
    typedef struct { int tile_x, tile_y, dist_sq; } PendingTile;
    PendingTile pending[32];
    int pending_count = 0;

    int tiles_rendered = 0;

    // Pass 1: render already-cached tiles immediately; queue uncached ones with a placeholder
    for (int ty = 0; ty < tiles_y; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            int tile_x = start_tile_x + tx;
            int tile_y = start_tile_y + ty;

            if (tile_x < 0 || tile_x >= max_tile || tile_y < 0 || tile_y >= max_tile)
                continue;

            int render_x = (tile_x * TILE_SIZE) - screen_world_x + offset_x;
            int render_y = (tile_y * TILE_SIZE) - screen_world_y + offset_y;

            if (render_x + TILE_SIZE < 0 || render_x >= screen_width ||
                render_y + TILE_SIZE < 0 || render_y >= screen_height)
                continue;

            CachedTile* cached = find_cached_tile(tile_x, tile_y, zoom);
            if (cached && cached->valid && cached->image.tex) {
                cached->last_used = cache_counter++;
                C2D_DrawImageAt(cached->image, render_x, render_y, 0.0f, NULL, 1.0f, 1.0f);
                tiles_rendered++;
            } else {
                // Draw placeholder now; tile will be loaded in pass 2
                C2D_DrawRectSolid(render_x, render_y, 0.0f, TILE_SIZE, TILE_SIZE,
                                C2D_Color32f(0.3f, 0.3f, 0.3f, 1.0f));
                C2D_DrawRectSolid(render_x + 10, render_y + 10, 0.1f, TILE_SIZE - 20, 4,
                                C2D_Color32f(1.0f, 0.0f, 0.0f, 0.8f));
                C2D_DrawRectSolid(render_x + 10, render_y + TILE_SIZE - 14, 0.1f, TILE_SIZE - 20, 4,
                                C2D_Color32f(1.0f, 0.0f, 0.0f, 0.8f));

                if (pending_count < 32) {
                    int dx = tile_x - center_tile_x;
                    int dy = tile_y - center_tile_y;
                    pending[pending_count].tile_x  = tile_x;
                    pending[pending_count].tile_y  = tile_y;
                    pending[pending_count].dist_sq = dx * dx + dy * dy;
                    pending_count++;
                }
            }
        }
    }

    // Sort pending tiles by distance from center — insertion sort (at most ~12 tiles)
    for (int i = 1; i < pending_count; i++) {
        PendingTile key = pending[i];
        int j = i - 1;
        while (j >= 0 && pending[j].dist_sq > key.dist_sq) {
            pending[j + 1] = pending[j];
            j--;
        }
        pending[j + 1] = key;
    }

    // Pass 2: download uncached tiles center-first, overdraw placeholder with real tile
    for (int i = 0; i < pending_count; i++) {
        int tile_x = pending[i].tile_x;
        int tile_y = pending[i].tile_y;

        int render_x = (tile_x * TILE_SIZE) - screen_world_x + offset_x;
        int render_y = (tile_y * TILE_SIZE) - screen_world_y + offset_y;

        CachedTile* slot = get_cache_slot();
        if (slot && load_tile_from_network(tile_x, tile_y, zoom, slot)) {
            slot->tile_x   = tile_x;
            slot->tile_y   = tile_y;
            slot->zoom     = zoom;
            slot->valid    = true;
            slot->last_used = cache_counter++;
            C2D_DrawImageAt(slot->image, render_x, render_y, 0.0f, NULL, 1.0f, 1.0f);
            tiles_rendered++;
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
    
    // Debug info occasionally
    if (debug_counter % 60 == 0 && is_top_screen) {
        printf("Rendered %d tiles\n", tiles_rendered);
    }
}

void map_tiles_clear_cache(void) {
    if (!initialized) return;
    
    // Mark all tiles as invalid
    for (int i = 0; i < MAX_CACHED_TILES; i++) {
        tile_cache[i].valid = false;
    }
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
        if (tile_cache[i].valid &&
            tile_cache[i].tile_x == tile_x &&
            tile_cache[i].tile_y == tile_y &&
            tile_cache[i].zoom == zoom) {
            return &tile_cache[i];
        }
    }
    return NULL;
}

static CachedTile* get_cache_slot(void) {
    // First, try to find an empty slot
    for (int i = 0; i < MAX_CACHED_TILES; i++) {
        if (!tile_cache[i].valid) {
            return &tile_cache[i];
        }
    }
    
    // If no empty slot, find the least recently used one
    u32 oldest_time = cache_counter;
    int oldest_index = 0;
    
    for (int i = 0; i < MAX_CACHED_TILES; i++) {
        if (tile_cache[i].last_used < oldest_time) {
            oldest_time = tile_cache[i].last_used;
            oldest_index = i;
        }
    }
    
    // Invalidate the old tile
    tile_cache[oldest_index].valid = false;
    return &tile_cache[oldest_index];
}

static bool load_tile_from_network(int tile_x, int tile_y, int zoom, CachedTile* tile) {
    if (!is_connected()) {
        return false;
    }
    
    u8* buffer = NULL;
    u32 size = 0;
    
    // Try to download the tile
    if (!download_tile(tile_x, tile_y, zoom, &buffer, &size)) {
        return false;
    }

    // Validate downloaded data
    if (!buffer || size < 8) {
        if (buffer) free(buffer);
        return false;
    }

    // Create image from downloaded data
    tile->image = create_image_from_buffer(buffer, size);

    // Free the download buffer now that PNG decoder has used it
    if (buffer) {
        free(buffer);
    }

    // If image creation failed, treat as a download failure
    if (!tile->image.tex) {
        return false;
    }

    return true;
}

static C2D_Image create_image_from_buffer(u8* buffer, u32 size) {
    C2D_Image image = {0};
    
    // Try to decode the PNG data
    int width, height;
    unsigned char* pixels = decode_png(buffer, size, &width, &height);
    
    if (!pixels) {
        printf("Error: Failed to decode PNG data\n");
        return image;
    }
    
    if (width <= 0 || height <= 0) {
        printf("Error: Invalid image dimensions: %dx%d\n", width, height);
        free_png_data(pixels);
        return image;
    }
    
    // Create C3D texture
    C3D_Tex* tex = (C3D_Tex*)malloc(sizeof(C3D_Tex));
    if (!tex) {
        printf("Error: Failed to allocate texture memory\n");
        free_png_data(pixels);
        return image;
    }
    
    // Use RGBA8 format to match PNG decoder output (4 bytes per pixel)
    if (!C3D_TexInit(tex, width, height, GPU_RGBA8)) {
        printf("Error: Failed to initialize C3D texture\n");
        free(tex);
        free_png_data(pixels);
        return image;
    }
    
    // Upload RGBA pixel data directly
    C3D_TexUpload(tex, pixels);
    
    // Set texture parameters
    C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    
    // No cleanup needed since we're using pixels directly    // Create subtexture descriptor following the blog post approach
    Tex3DS_SubTexture* subtex = (Tex3DS_SubTexture*)malloc(sizeof(Tex3DS_SubTexture));
    if (!subtex) {
        printf("Error: Failed to allocate subtexture memory\n");
        C3D_TexDelete(tex);
        free(tex);
        free_png_data(pixels);
        return image;
    }
    
    // Set up subtexture coordinates - flip vertically to fix upside down
    subtex->width = width;
    subtex->height = height;
    subtex->left = 0.0f;
    subtex->top = 1.0f;    // Flip Y: swap top/bottom
    subtex->right = 1.0f;
    subtex->bottom = 0.0f; // Flip Y: swap top/bottom
    
    // Set up the C2D_Image
    image.tex = tex;
    image.subtex = subtex;
    
    free_png_data(pixels);
    printf("Successfully created %dx%d tile image from PNG using libpng\n", width, height);
    return image;
}
