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

// Tile coordinate conversion functions
void lat_lon_to_tile(double lat, double lon, int zoom, int* tile_x, int* tile_y);
void tile_to_lat_lon(int tile_x, int tile_y, int zoom, double* lat, double* lon);
void lat_lon_to_pixel(double lat, double lon, int zoom, int* pixel_x, int* pixel_y);

#endif // MAP_TILES_H
