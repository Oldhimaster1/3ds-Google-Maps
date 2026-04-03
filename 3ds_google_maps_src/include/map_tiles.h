#ifndef MAP_TILES_H
#define MAP_TILES_H

#include <3ds.h>
#include <stdbool.h>

// Map tile system functions
bool map_tiles_init(void);
void map_tiles_cleanup(void);
void map_tiles_render(double lat, double lon, int zoom, int offset_x, int offset_y, 
                     int screen_width, int screen_height, bool is_top_screen);
void map_tiles_clear_cache(void);

// Call once per frame from the main thread to finish GPU texture uploads
// for tiles that were downloaded in the background.
void map_tiles_process_downloads(void);

// Prefetch tiles in a ring around the current view.
// Call periodically (e.g., once per frame) when the user is idle.
// Internally throttled to avoid downloading too many tiles at once.
void map_tiles_prefetch_step(double lat, double lon, int zoom,
                             int screen_width, int screen_height,
                             int ring);

// Reset any in-progress prefetch work (useful after a big jump/zoom).
void map_tiles_prefetch_reset(void);

// Cache stats (RAM tile cache)
u32 map_tiles_get_cache_used(void);
u32 map_tiles_get_cache_capacity(void);

// Tile coordinate conversion functions
void lat_lon_to_tile(double lat, double lon, int zoom, int* tile_x, int* tile_y);
void tile_to_lat_lon(int tile_x, int tile_y, int zoom, double* lat, double* lon);
void lat_lon_to_pixel(double lat, double lon, int zoom, int* pixel_x, int* pixel_y);

#endif // MAP_TILES_H
