#ifndef PANO_DETAIL_H
#define PANO_DETAIL_H
#include <stdbool.h>
#include <stdint.h>
#define PANO_VISIBLE_MAX 12
typedef struct {uint8_t x,y,z;float score;} PanoDetailTile;
/* Selects only the equirectangular tile coordinates currently visible.
 * Retrieval and provider permissions belong to the caller. */
int pano_detail_visible(float yaw_rad,float pitch_rad,float fov_deg,int zoom,
                        PanoDetailTile out[PANO_VISIBLE_MAX]);
#endif
