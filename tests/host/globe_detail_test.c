#include "globe_detail.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 GlobePatch a=globe_patch_for_view(40.7128,-74.006,4,1);
 assert(a.z==0&&a.x==0&&a.y==0);
 GlobePatch b=globe_patch_for_view(40.7128,-74.006,1.6,2);
 assert(b.z>=3&&b.z<=5 && b.x>=0&&b.y>=0);
 double lat,lon;globe_patch_geo(&b,.5,.5,&lat,&lon);assert(lat<b.north&&lat>b.south);
 for(int x=-90;x<90;x+=5){GlobePatch p=globe_patch_for_view(x,179.9,1.5,2);assert(p.x>=0&&p.x<(1<<p.z));}
 puts("Progressive globe image patch LOD tests passed");return 0;
}
