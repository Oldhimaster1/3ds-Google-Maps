#ifndef MAP_EXPLORE_H
#define MAP_EXPLORE_H

#include <citro3d.h>
#include "terrain_math.h"
#include "building_mesh.h"

typedef enum { EXPLORE_NONE, EXPLORE_GLOBE, EXPLORE_TERRAIN, EXPLORE_STREET } ExploreMode;
typedef struct { const char *name; double lat, lon; } ExplorePlace;

extern const ExplorePlace explore_terrain_place;

/* Load outside a frame. Failure preserves the previous scene. */
bool explore_load(ExploreMode mode, unsigned street_stop);
/* Accept already Morton-tiled 1024x512 RGB565 bytes; preserve old image on failure. */
bool explore_load_street_pixels(const void *tiled_rgb565, bool is_pano);
/* Apply one decoded live Terrarium tile to the terrain scene on render thread. */
bool explore_apply_terrain(const TerrainGrid *grid,float span_m,float exaggeration,
                           float focus_x,float focus_z);
/* Add actual building extrusions into the same stereo geographic scene. */
bool explore_apply_buildings(const BuildingMesh *mesh);
void explore_unload(void);
void explore_draw(C3D_RenderTarget *target, float yaw, float pitch,
                  float distance, float fov, float eye);

#endif
