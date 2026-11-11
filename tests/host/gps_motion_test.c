#include "gps_motion.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 GpsMotion m={0};
 assert(gps_motion_update(&m,40,-74,10,0,NAN,1000));
 assert(m.has_speed && !m.has_heading);
 assert(gps_motion_update(&m,40.000001,-74.0,10,NAN,NAN,2000));
 assert(!m.has_speed&&!m.has_heading); // jitter gets rejected
 assert(gps_motion_update(&m,40.001,-74,3,11,0,3000));
 assert(m.has_heading&&fabs(m.heading_deg)<1e-5 && m.speed_mps==11);
 assert(gps_motion_update(&m,40.0012,-74.0,3,NAN,NAN,4000));
 assert(m.has_heading&&m.heading_deg<1.0&&m.speed_mps>10);
 assert(gps_motion_update(&m,0,179.999,3,1,45,5000));
 assert(gps_motion_update(&m,0,-179.999,3,NAN,NAN,9000));
 assert(m.has_heading&&m.heading_deg>85&&m.heading_deg<95);
 assert(!gps_motion_update(&m,NAN,0,4,4,45,7000));
 puts("GPS speed, direction, jitter and dateline tests passed");return 0;
}
