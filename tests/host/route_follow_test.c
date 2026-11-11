#include "route_follow.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 RouteFollow r={0};RouteFollowPoint p[]={{40.0,-74.0},{40.0,-73.99},{40.01,-73.99}};
 assert(route_follow_begin(&r,p,3,2000,600));
 assert(route_follow_update(&r,40.0,-74.0,5,1000));
 assert(r.progress_m<1 && fabs(r.remaining_m-2000)<1 && !r.off_route);
 assert(route_follow_update(&r,40.0,-73.995,5,2000));
 assert(r.progress_m>250 && r.progress_m<950 && r.eta_s>300);
 assert(route_follow_update(&r,40.005,-73.99,5,3000));
 assert(r.progress_m>950 && r.remaining_m<1050);
 assert(route_follow_update(&r,40.01,-73.99,5,4000));
 assert(r.remaining_m<1 && r.eta_s<1);
 for(int i=0;i<3;i++)assert(route_follow_update(&r,40.007,-73.98,5,10000+i*1000));
 assert(route_follow_needs_reroute(&r,12000));
 assert(!route_follow_needs_reroute(&r,12500));
 for(int i=0;i<3;i++)assert(route_follow_update(&r,40.007,-73.98,5,13000+i*1000));
 assert(!route_follow_needs_reroute(&r,16000));
 assert(route_follow_needs_reroute(&r,43001));
 assert(!route_follow_update(&r,NAN,0,5,50000));
 route_follow_reset(&r);
 RouteFollowPoint dateline[]={{0.,179.9},{0.,-179.9}};
 assert(route_follow_begin(&r,dateline,2,0,0));
 assert(route_follow_update(&r,0,180,2,1000));
 assert(r.progress_m>10000 && r.remaining_m>10000);
 route_follow_reset(&r);
 puts("Route progress, rerouting debounce, dateline, GPS quality tests passed");return 0;
}
