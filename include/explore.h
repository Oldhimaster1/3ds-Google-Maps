#ifndef MAP_EXPLORE_H
#define MAP_EXPLORE_H

#include <citro3d.h>

typedef enum { EXPLORE_NONE, EXPLORE_GLOBE, EXPLORE_TERRAIN, EXPLORE_STREET } ExploreMode;
typedef struct { const char *name; double lat, lon; } ExplorePlace;

extern const ExplorePlace explore_street_places[2];
extern const ExplorePlace explore_terrain_place;

/* Load outside a frame. Failure preserves the previous scene. */
bool explore_load(ExploreMode mode, unsigned street_stop);
/* Accept already Morton-tiled 1024x512 RGB565 bytes; preserve old image on failure. */
bool explore_load_street_pixels(const void *tiled_rgb565, bool is_pano);
void explore_unload(void);
void explore_draw(C3D_RenderTarget *target, float yaw, float pitch,
                  float distance, float fov, float eye);

#endif
