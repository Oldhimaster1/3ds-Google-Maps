#include "pano_detail.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
int main(void){PanoDetailTile t[PANO_VISIBLE_MAX];
 int n=pano_detail_visible(0,0,35,3,t);assert(n>0&&n<=PANO_VISIBLE_MAX);
 for(int i=0;i<n;i++){assert(t[i].x<8&&t[i].y<4&&t[i].z==3);if(i)assert(t[i].score>=t[i-1].score);}
 assert(!pano_detail_visible(0,0,35,1,t));
 assert(!pano_detail_visible(NAN,0,35,3,t));
 for(int a=-360;a<=360;a+=5)for(int p=-80;p<=80;p+=10){
  n=pano_detail_visible(a*.017453292519943295f,p*.017453292519943295f,30,4,t);
  assert(n<=12&&n>=0);
  for(int j=0;j<n;j++){assert(t[j].x<16&&t[j].y<8&&t[j].z==4);}
 }
 puts("High-resolution panorama viewport tile selection passed");return 0;
}
