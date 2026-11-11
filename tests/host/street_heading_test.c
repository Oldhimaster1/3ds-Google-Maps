#include "street_heading.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
int main(void){float y=0;
 assert(street_heading_transfer(90,0,270,&y));assert(fabsf(fabsf(y)-3.1415926f)<1e-5);
 assert(street_heading_transfer(315,.785398f,0,&y));assert(fabsf(y)<1e-5);
 assert(street_heading_transfer(0,0,90,&y));assert(fabsf(y+1.57079633f)<1e-5);
 for(int a=-720;a<=720;a+=10)for(int b=-720;b<=720;b+=10){
  float old=.2345f;
  assert(street_heading_transfer(a,old,b,&y));
  float old_world=remainderf(a*.017453292519943295f+old,6.283185307179586f);
  float new_world=remainderf(b*.017453292519943295f+y,6.283185307179586f);
  assert(fabsf(remainderf(old_world-new_world,6.283185307179586f))<.00002f);
 }
 assert(!street_heading_transfer(NAN,0,0,&y));
 puts("Panorama heading preservation tests passed");return 0;
}
