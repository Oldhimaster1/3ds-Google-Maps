#include "detail_plan.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static unsigned seed=0x317421u;
static unsigned random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){
 DetailKey k={0};char url[256];
 const char *id="1234567890123456789012";
 assert(!detail_plan_panorama(0,0,55,0,&k));
 assert(detail_plan_panorama(0,0,55,3,&k) && k.kind==DETAIL_PANORAMA&&k.z==2);
 assert(detail_plan_panorama(0,0,29,2,&k) && k.z==4);
 assert(detail_plan_url(k,id,url,sizeof(url)));
 assert(strstr(url,"panoid=1234567890123456789012"));
 assert(!detail_plan_url(k,"../bad",url,sizeof(url)));
 assert(!detail_plan_url(k,id,url,12));
 assert(detail_plan_globe(40,-74,1.6f,2,&k)&&k.kind==DETAIL_GLOBE);
 assert(detail_plan_url(k,NULL,url,sizeof(url)));
 assert(strstr(url,"gibs.earthdata.nasa.gov"));
 assert(!detail_plan_globe(NAN,0,2,1,&k));
 assert(!detail_plan_globe(100,0,2,1,&k));
 assert(detail_plan_globe(45,180,2,2,&k));
 assert(detail_plan_globe(45,-180,2,2,&k));
 for(unsigned i=0;i<100000;i++){
  double lat=(random32()%1800000)/10000.0-90.0;
  double lon=(random32()%3600000)/10000.0-180.0;
  float dist=1.45f+(random32()%3700)/1000.f;
  assert(detail_plan_globe(lat,lon,dist,random32()%3,&k));
  assert(detail_plan_url(k,NULL,url,sizeof(url)));
  float yaw=(float)((int)(random32()%62800)-31400)/10000.0f;
  float pitch=(float)((int)(random32()%28000)-14000)/10000.0f;
  float fov=28+(random32()%5800)/100.0f;
  if(detail_plan_panorama(yaw,pitch,fov,1+random32()%3,&k)){
   assert(k.kind==DETAIL_PANORAMA && k.x<(1u<<k.z) &&
          k.y<(1u<<(k.z-1)));
   assert(detail_plan_url(k,id,url,sizeof(url)));
  }
 }
 assert(detail_plan_equal((DetailKey){1,2,0,1},(DetailKey){1,2,0,1}));
 assert(!detail_plan_equal((DetailKey){1,2,0,1},(DetailKey){1,2,1,1}));
 puts("detail plan: PASS");return 0;
}
