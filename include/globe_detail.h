#ifndef GLOBE_DETAIL_H
#define GLOBE_DETAIL_H
#include <stdbool.h>
/* Select one Web Mercator image patch around the center of the view. */
typedef struct {int z,x,y;double north,south,east,west;} GlobePatch;
GlobePatch globe_patch_for_view(double lat,double lon,float camera_distance,unsigned detail);
void globe_patch_geo(const GlobePatch *p,float u,float v,double *lat,double *lon);
#endif
