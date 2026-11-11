#ifndef DETAIL_PLAN_H
#define DETAIL_PLAN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum {DETAIL_NONE=0,DETAIL_PANORAMA=1,DETAIL_GLOBE=2} DetailKind;
typedef struct {uint8_t kind,z,x,y;} DetailKey;
bool detail_plan_panorama(float yaw,float pitch,float fov,unsigned quality,DetailKey *out);
bool detail_plan_globe(double lat,double lon,float distance,unsigned quality,DetailKey *out);
bool detail_plan_equal(DetailKey a,DetailKey b);
bool detail_plan_url(DetailKey key,const char *pano_id,char *url,size_t capacity);
#endif
