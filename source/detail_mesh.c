#include "detail_mesh.h"
#include <math.h>
#include <stdint.h>
bool detail_mesh_point(DetailKey key,float u,float v,float *x,float *y,float *z){
 if(!x||!y||!z||!isfinite(u)||!isfinite(v)||u<0||u>1||v<0||v>1||
    key.z>8||key.x>=(1u<<key.z)||key.y>=(1u<<key.z))return false;
 const double pi=3.14159265358979323846;
 const double ntiles=(double)(1u<<key.z);
 double lon=((key.x+u)/ntiles*2.-1.)*pi;
 double lat;
 if(key.kind==DETAIL_PANORAMA){
  if(key.z<2||key.z>4||key.y>=(1u<<(key.z-1)))return false;
  lat=(.5-(key.y+v)/(ntiles*.5))*pi;
 }else if(key.kind==DETAIL_GLOBE){
  if(key.z>8)return false;
  double merc_y=(key.y+v)/ntiles;
  lat=atan(sinh(pi*(1.-2.*merc_y)));
 }else return false;
 float radius=key.kind==DETAIL_PANORAMA?.985f:1.014f;
 float ground=radius*(float)cos(lat);
 *x=ground*(float)sin(lon);
 *y=radius*(float)sin(lat);
 *z=(key.kind==DETAIL_PANORAMA?-ground:ground)*(float)cos(lon);
 return isfinite(*x)&&isfinite(*y)&&isfinite(*z);
}
