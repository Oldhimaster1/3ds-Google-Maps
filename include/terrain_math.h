#ifndef TERRAIN_MATH_H
#define TERRAIN_MATH_H
#include <stdint.h>
#include <stdbool.h>
#define TERRAIN_GRID 33
#define TERRAIN_POINTS (TERRAIN_GRID*TERRAIN_GRID)
typedef struct {float height[TERRAIN_POINTS];float min_height,max_height;} TerrainGrid;
float terrarium_height(uint8_t r,uint8_t g,uint8_t b);
bool terrain_decode_grid(const uint8_t *rgb,int width,int height,int channels,
                         TerrainGrid *out);
void terrain_web_tile(double latitude,double longitude,int zoom,
                      int *tile_x,int *tile_y);
/* Compute terrain height and horizontal length in consistently scaled metres
 * for a local patch; z exaggeration defaults to 1. */
void terrain_world_vertex(const TerrainGrid *grid,int i,int j,
                          float span_m,float exaggeration,
                          float *x,float *y,float *z);
#endif
