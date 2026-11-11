#include "geo_formats.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static void test(const char*s,double lat,double lon,GeoFormat expected){
  double a=0,b=0;GeoFormat got=geo_parse_coordinates(s,&a,&b);
  if(got!=expected||fabs(a-lat)>0.0003||fabs(b-lon)>0.0003)
     fprintf(stderr,"FAIL %s: got %d %.8f %.8f expected %d %.8f %.8f\n",s,got,a,b,expected,lat,lon);
  assert(got==expected&&fabs(a-lat)<.0003&&fabs(b-lon)<.0003);
}
int main(void){
  test("40.7128, -74.0060",40.7128,-74.006,GEO_DECIMAL);
  test("40.7128 N 74.0060 W",40.7128,-74.006,GEO_DECIMAL);
  test("40\xC2\xB0""42'46\"N 74\xC2\xB0""00'22\"W",40.7127778,-74.0061111,GEO_DMS);
  test("40:42:46 N, 74:00:22 W",40.7127778,-74.0061111,GEO_DMS);
  test("87G8Q2VV+4W",40.7928125,-73.9551875,GEO_PLUS_CODE);
  double a=123,b=123;
  assert(geo_parse_coordinates("40.7128 200",&a,&b)==GEO_INVALID);
  assert(geo_parse_coordinates("40\xC2\xB0""67' N, 74\xC2\xB0""00'W",&a,&b)==GEO_INVALID);
  assert(geo_parse_coordinates("Q2VV+4W",&a,&b)==GEO_INVALID);
  assert(geo_parse_coordinates("90.01,0",&a,&b)==GEO_INVALID);
  assert(geo_parse_coordinates("-40N,74W",&a,&b)==GEO_INVALID);
  puts("Coordinate formats passed");return 0;
}
