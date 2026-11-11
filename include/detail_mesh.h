#ifndef DETAIL_MESH_H
#define DETAIL_MESH_H
#include "detail_plan.h"
/* Position a fragment of equirectangular panorama (inside sphere) or NASA
 * Web Mercator globe imagery (outside sphere) using the existing camera axes.
 * u/v are each 0..1 relative to the selected image tile. */
bool detail_mesh_point(DetailKey key,float u,float v,float *x,float *y,float *z);
#endif
