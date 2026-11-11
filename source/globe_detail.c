#include "globe_detail.h"
#include <math.h>
static double pi(void){return 3.1415926535897932384626433832795;}
static double inv_merc(double y){return atan(sinh(pi()*(1.-2.*y)))*180./pi();}
GlobePatch globe_patch_for_view(double lat,double lon,float dist,unsigned detail){
 GlobePatch p={0};if(!isfinite(lat)||!isfinite(lon)||!isfinite(dist))return p;
 int z=dist>3.5f?0:dist>2.7f?1:dist>2.f?2:dist>1.6f?3:4;
 if(detail==0&&z>1)z=1;else if(detail>=2&&z<4)z++;
 if(z>5)z=5;
 lat=fmax(-85.05112878,fmin(85.05112878,lat));
 lon=remainder(lon,360.);
 int world=1<<z;
 p.z=z;p.x=(int)floor((lon+180.)/360.*world);
 if(p.x==world)p.x=0;
 p.y=(int)floor((1.-asinh(tan(lat*pi()/180.))/pi())*.5*world);
 if(p.y<0)p.y=0;
 if(p.y>=world)p.y=world-1;
 p.west=(double)p.x/world*360.-180.;p.east=(double)(p.x+1)/world*360.-180.;
 p.north=inv_merc((double)p.y/world);p.south=inv_merc((double)(p.y+1)/world);
 return p;
}
void globe_patch_geo(const GlobePatch *p,float u,float v,double *lat,double *lon){
 if(!p||!lat||!lon)return;
 *lon=p->west+(p->east-p->west)*u;
 *lat=inv_merc(((double)p->y+v)/(1<<p->z));
}
