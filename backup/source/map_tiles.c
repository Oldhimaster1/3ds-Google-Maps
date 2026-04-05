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

static void free_cached_tile(CachedTile* tile) {
    if (!tile) return;

    if (tile->image.tex) {
        C3D_TexDelete(tile->image.tex);
        free(tile->image.tex);
        tile->image.tex = NULL;
    }
    if (tile->image.subtex) {
        free(tile->image.subtex);
        tile->image.subtex = NULL;
    }

    tile->valid = false;
    tile->tile_x = 0;
    tile->tile_y = 0;
    tile->zoom = 0;
    tile->last_used = 0;
}

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
        free_cached_tile(&tile_cache[i]);
    }
    
    initialized = false;
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
    
    static int debug_counter = 0;
    debug_counter++;
    
    // Debug: Print tile info occasionally
    if (debug_counter % 60 == 0 && is_top_screen) {
        printf("Rendering tiles at zoom %d, center: %d,%d\n", zoom, center_tile_x, center_tile_y);
    }
    
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
            if (!cached || !cached->valid) {
                // Tile not in cache, try to load it
                cached = get_cache_slot();
                if (cached && load_tile_from_network(tile_x, tile_y, zoom, cached)) {
                    cached->tile_x = tile_x;
                    cached->tile_y = tile_y;
                    cached->zoom = zoom;
                    cached->valid = true;
                    cached->last_used = cache_counter++;
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
    
    // Debug info occasionally
    if (debug_counter % 60 == 0 && is_top_screen) {
        printf("Rendered %d tiles\n", tiles_rendered);
    }
}

void map_tiles_clear_cache(void) {
    if (!initialized) return;
    
    // Free all cached tiles
    for (int i = 0; i < MAX_CACHED_TILES; i++) {
        free_cached_tile(&tile_cache[i]);
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
            // Ensure any stale resources are released
            free_cached_tile(&tile_cache[i]);
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
    
    // Evict the old tile and free its resources
    free_cached_tile(&tile_cache[oldest_index]);
    return &tile_cache[oldest_index];
}

static bool load_tile_from_network(int tile_x, int tile_y, int zoom, CachedTile* tile) {
    printf("Attempting to load tile %d,%d zoom %d\n", tile_x, tile_y, zoom);

    // If this slot previously held a tile, release resources before reusing
    free_cached_tile(tile);
    
    if (!is_connected()) {
        printf("ERROR: Not connected to network!\n");
        return false;
    }
    
    printf("Network connection confirmed\n");
    
    u8* buffer = NULL;
    u32 size = 0;
    
    // Try to download the tile
    if (!download_tile(tile_x, tile_y, zoom, &buffer, &size)) {
        printf("ERROR: Failed to download tile %d,%d zoom %d\n", tile_x, tile_y, zoom);
        return false;
    }
    
    printf("Downloaded tile %d,%d zoom %d: %u bytes\n", tile_x, tile_y, zoom, size);
    
    // Check if data looks like PNG (starts with PNG magic bytes)
    if (size < 8 || buffer[0] != 0x89 || buffer[1] != 0x50 || buffer[2] != 0x4E || buffer[3] != 0x47) {
        printf("ERROR: Downloaded data is NOT a PNG file! First 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
        if (buffer) free(buffer);
        return false;
    }
    
    printf("PNG magic bytes confirmed - data looks valid\n");
    
    // Create image from downloaded data
    tile->image = create_image_from_buffer(buffer, size);
    
    // Free the buffer
    if (buffer) {
        free(buffer);
    }
    
    return true;
}

static C2D_Image create_image_from_buffer(u8* buffer, u32 size) {
    C2D_Image image = {0};
    // Decode + tile PNG data for 3DS textures (RGBA8)
    int width, height;
    unsigned char* pixels = decode_png(buffer, (int)size, &width, &height);

    if (!pixels) {
        printf("Error: Failed to decode PNG data\n");
        return image;
    }

    if (width != 256 || height != 256) {
        printf("Error: Invalid tile dimensions: %dx%d (expected 256x256)\n", width, height);
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

    // Initialize RGBA8 texture
    if (!C3D_TexInit(tex, width, height, GPU_RGBA8)) {
        printf("Error: Failed to initialize C3D texture\n");
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
        printf("Error: Failed to allocate subtexture memory\n");
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
    printf("Successfully created %dx%d RGBA8 texture\n", width, height);
    return image;
}
