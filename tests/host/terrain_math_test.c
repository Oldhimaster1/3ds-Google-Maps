#include "terrain_math.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
 assert(fabsf(terrarium_height(128,0,0))<1e-5);
 assert(fabsf(terrarium_height(127,255,0)+1)<1e-5);
 assert(fabsf(terrarium_height(128,1,128)-1.5f)<1e-5);
 uint8_t *a=malloc(256*256*3);assert(a);
 for(int y=0;y<256;y++)for(int x=0;x<256;x++){
  uint8_t *p=a+(y*256+x)*3;p[0]=128;p[1]=(uint8_t)(x/16);p[2]=0;
 }
 TerrainGrid g;assert(terrain_decode_grid(a,256,256,3,&g));
 assert(g.min_height==0&&g.max_height==15);
 float x,y,z;terrain_world_vertex(&g,16,16,1000,2,&x,&y,&z);
 assert(fabsf(x)<1e-7 && fabsf(z)<1e-7 && isfinite(y));
 int tx,ty;terrain_web_tile(40.7128,-74.006,10,&tx,&ty);
 assert(tx==301&&ty==385);terrain_web_tile(0,180,3,&tx,&ty);assert(tx==0&&ty==4);
 free(a);puts("Terrarium decoding and mesh geometry tests passed");return 0;
}
