#ifndef DETAIL_RUNTIME_H
#define DETAIL_RUNTIME_H
#include "panorama_live.h"
/* Run on UI thread, outside Citro3D frame. One bounded image worker and one
 * GPU detail patch are shared between panorama and globe modes. */
void detail_runtime_update_panorama(const PanoLiveImage *p,float yaw,float pitch,
                                    float fov,unsigned quality);
void detail_runtime_update_globe(double lat,double lon,float distance,unsigned quality);
void detail_runtime_stop(void);
const char *detail_runtime_status(void);
#endif
