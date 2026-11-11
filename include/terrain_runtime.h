#ifndef MAP_TERRAIN_RUNTIME_H
#define MAP_TERRAIN_RUNTIME_H
#include "terrain_math.h"
#include <stdbool.h>
/* One small, interactive elevation request for the user's selected map point.
 * Network and image decode run in a worker; GPU upload stays on main thread.
 * Does not download offline packs or retain an imagery dataset. */
bool terrain_runtime_start(double lat,double lon,int zoom);
bool terrain_runtime_poll(TerrainGrid *out,float *span_m,
                          float *focus_x,float *focus_z);
void terrain_runtime_stop(void);
const char *terrain_runtime_status(void);
#endif
