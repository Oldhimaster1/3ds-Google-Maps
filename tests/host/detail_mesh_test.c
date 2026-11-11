#include "detail_mesh.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
static uint32_t state=0x1fedc5b3u;
static uint32_t next(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static void check(DetailKey k,float u,float v,float expected){
 float x=0,y=0,z=0;
 assert(detail_mesh_point(k,u,v,&x,&y,&z));
 float radius=sqrtf(x*x+y*y+z*z);
 assert(fabsf(radius-expected)<.0005f);
 assert(isfinite(x)&&isfinite(y)&&isfinite(z));
}
int main(void){
 float x,y,z;
 check((DetailKey){DETAIL_GLOBE,0,0,0},.5f,.5f,1.014f);
 assert(detail_mesh_point((DetailKey){DETAIL_GLOBE,0,0,0},.5f,.5f,&x,&y,&z));
 assert(fabsf(x)<.0001f&&fabsf(y)<.0001f&&fabsf(z-1.014f)<.0001f);
 check((DetailKey){DETAIL_PANORAMA,2,2,1},0,0,.985f);
 assert(detail_mesh_point((DetailKey){DETAIL_PANORAMA,2,2,1},0,0,&x,&y,&z));
 assert(fabsf(z+.985f)<.0001f);
 for(unsigned i=0;i<100000;i++){
  unsigned kind=(next()&1)?DETAIL_GLOBE:DETAIL_PANORAMA;
  unsigned zoom=kind==DETAIL_GLOBE?next()%7u:2+next()%3u;
  unsigned nx=1u<<zoom,ny=kind==DETAIL_GLOBE?nx:nx/2u;
  DetailKey k={(uint8_t)kind,(uint8_t)zoom,(uint8_t)(next()%nx),
               (uint8_t)(next()%ny)};
  float u=(next()%10001u)/10000.f,v=(next()%10001u)/10000.f;
  check(k,u,v,kind==DETAIL_GLOBE?1.014f:.985f);
 }
 assert(!detail_mesh_point((DetailKey){DETAIL_PANORAMA,2,0,2},0,0,&x,&y,&z));
 assert(!detail_mesh_point((DetailKey){DETAIL_GLOBE,4,0,0},-1,0,&x,&y,&z));
 assert(!detail_mesh_point((DetailKey){DETAIL_GLOBE,4,0,0},0,0,NULL,&y,&z));
 puts("detail geometry: PASS");return 0;
}
