#include "detail_plan.h"
#include "pano_detail.h"
#include "globe_detail.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static bool valid_google_id(const char *s){
 if(!s||strlen(s)!=22)return false;
 for(const unsigned char *p=(const unsigned char*)s;*p;p++)
  if(!( (*p>='0'&&*p<='9') || (*p>='a'&&*p<='z') ||
        (*p>='A'&&*p<='Z') || *p=='_' || *p=='-' ))return false;
 return true;
}
bool detail_plan_equal(DetailKey a,DetailKey b){
 return a.kind==b.kind && a.z==b.z && a.x==b.x && a.y==b.y;
}
bool detail_plan_panorama(float yaw,float pitch,float fov,unsigned quality,DetailKey *out){
 if(!out||quality>3||quality==0||!isfinite(yaw)||!isfinite(pitch)||!isfinite(fov))return false;
 /* No full-resolution equirectangular allocation: only one visible 512px tile.
  * Auto chooses 2/3 based on field-of-view; Detail can request zoom 4. */
 unsigned z=quality==1?2:quality==2?(fov<=32?4:3):(fov<37?3:2);
 PanoDetailTile tiles[PANO_VISIBLE_MAX];
 int count=pano_detail_visible(yaw,pitch,fov,(int)z,tiles);
 if(count<=0)return false;
 *out=(DetailKey){DETAIL_PANORAMA,(uint8_t)z,tiles[0].x,tiles[0].y};
 return true;
}
bool detail_plan_globe(double lat,double lon,float distance,unsigned quality,DetailKey *out){
 if(!out||quality>2||!isfinite(lat)||!isfinite(lon)||!isfinite(distance)||
    fabs(lat)>90||distance<1.0f||distance>10.0f)return false;
 GlobePatch patch=globe_patch_for_view(lat,lon,distance,quality);
 if(patch.z<0||patch.z>8||patch.x<0||patch.y<0||
    patch.x>=(1<<patch.z)||patch.y>=(1<<patch.z))return false;
 *out=(DetailKey){DETAIL_GLOBE,(uint8_t)patch.z,(uint8_t)patch.x,(uint8_t)patch.y};
 return true;
}
bool detail_plan_url(DetailKey k,const char *pano_id,char *url,size_t capacity){
 if(!url||capacity<16||k.z>8||k.x>=(1u<<k.z)||k.y>=(1u<<k.z))return false;
 int n=-1;
 if(k.kind==DETAIL_PANORAMA){
  if(k.z<2||k.z>4||!valid_google_id(pano_id))return false;
  n=snprintf(url,capacity,"https://streetviewpixels-pa.googleapis.com/v1/tile"
   "?cb_client=maps_sv.tactile&panoid=%s&x=%u&y=%u&zoom=%u",
   pano_id,(unsigned)k.x,(unsigned)k.y,(unsigned)k.z);
 }else if(k.kind==DETAIL_GLOBE){
  if(k.z>8)return false;
  /* NASA GIBS Blue Marble: on-demand global imagery, not real-time satellite. */
  n=snprintf(url,capacity,
   "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/"
   "BlueMarble_NextGeneration/default/GoogleMapsCompatible_Level8/%u/%u/%u.jpeg",
   (unsigned)k.z,(unsigned)k.y,(unsigned)k.x);
 }else return false;
 return n>0 && (size_t)n<capacity;
}
