#ifndef MAP_BUILDING_RUNTIME_H
#define MAP_BUILDING_RUNTIME_H
#include "building_mesh.h"
#include "terrain_math.h"
#include <stdbool.h>
/* Experimental, user-requested small-area OSM building fetch, not a
 * continuous public-Overpass tile crawler or required background service. */
bool building_runtime_start(double selected_lat,double selected_lon,
                            const TerrainGrid *terrain,float span_m,int zoom);
const BuildingMesh *building_runtime_poll(void);
void building_runtime_stop(void);
const char *building_runtime_status(void);
#endif
