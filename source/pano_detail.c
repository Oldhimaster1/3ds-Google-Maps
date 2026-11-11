#include "pano_detail.h"
#include <math.h>
#include <string.h>
int pano_detail_visible(float yaw,float pitch,float fov,int zoom,
                        PanoDetailTile out[PANO_VISIBLE_MAX]){
 if(!out||!isfinite(yaw)||!isfinite(pitch)||!isfinite(fov)||
    fov<15||fov>125||zoom<2||zoom>4)return 0;
 int cols=1<<zoom,rows=1<<(zoom-1);int count=0;
 float center=(float)remainder(yaw,6.283185307179586);
 /* Screen aspect 400:240. Spherical vertical FOV follows projection. */
 float h=fov*.017453292519943295f;
 float v=2*atanf(tanf(h*.5f)*.6f);
 float ur=center/6.283185307179586f+.5f;
 float vr=.5f-pitch/3.141592653589793f;
 float halfu=h/(2*6.283185307179586f),halfv=v/(2*3.141592653589793f);
 for(int ty=0;ty<rows;ty++)for(int tx=0;tx<cols;tx++){
  float u=((float)tx+.5f)/cols,vv=((float)ty+.5f)/rows;
  float dx=remainderf(u-ur,1.f),dy=vv-vr;
  if(fabsf(dx)>(halfu+1.f/cols*.5f) || fabsf(dy)>(halfv+1.f/rows*.5f))continue;
  float score=dx*dx+dy*dy;
  int j=count;
  if(j>=PANO_VISIBLE_MAX){if(score>=out[j-1].score)continue;j=PANO_VISIBLE_MAX-1;}
  else count++;
  while(j>0 && out[j-1].score>score){out[j]=out[j-1];j--;}
  out[j]=(PanoDetailTile){(uint8_t)tx,(uint8_t)ty,(uint8_t)zoom,score};
 }
 return count;
}
