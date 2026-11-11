#ifndef BUILDING_MESH_H
#define BUILDING_MESH_H
#include <stdbool.h>
#include <stdint.h>
#define BUILDING_FOOTPRINT_MAX 32
#define BUILDING_MESH_VERT_MAX 4096
#define BUILDING_MESH_IDX_MAX 8192
typedef struct {double lat,lon;} BuildingGeoPoint;
typedef struct {float x,y,z,u,v;} BuildingMeshVertex;
typedef struct {
    BuildingMeshVertex verts[BUILDING_MESH_VERT_MAX];
    uint16_t indices[BUILDING_MESH_IDX_MAX];
    unsigned vertex_count,index_count,buildings;
} BuildingMesh;
/* Caller supplies tile center and tile diameter, in metres. Building geometry
 * remains within the same world-space coordinates as terrain. */
bool building_mesh_add(BuildingMesh *mesh,const BuildingGeoPoint *path,unsigned count,
                       double center_lat,double center_lon,float tile_m,
                       float height_m,float base_height_m,float height_origin_m);
#endif
