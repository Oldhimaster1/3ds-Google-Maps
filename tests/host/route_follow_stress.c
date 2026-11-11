#include "route_follow.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t rng=0x57428a3u;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
 static RouteFollowPoint points[16000];
 for(size_t i=0;i<16000;i++)
    points[i]=(RouteFollowPoint){40.0+(i/16000.)*.5,-73.9+(i/16000.)*.4};
 RouteFollow r={0};
 assert(route_follow_begin(&r,points,16000,100000,12000));
 for(unsigned i=0;i<4000;i++){
    size_t index=next()%16000;
    double lat=points[index].lat+((int)(next()%7)-3)*.000001;
    double lon=points[index].lon+((int)(next()%7)-3)*.000001;
    assert(route_follow_update(&r,lat,lon,10,i*1000ULL));
    assert(isfinite(r.progress_m)&&isfinite(r.remaining_m)&&isfinite(r.cross_track_m));
    assert(r.progress_m>=0&&r.remaining_m>=0);
    assert(r.progress_m+r.remaining_m<=r.route_m+.01);
 }
 route_follow_reset(&r);
 assert(!route_follow_begin(&r,points,1,100,10));
 assert(!route_follow_begin(&r,points,16001,100,10));
 assert(!route_follow_begin(&r,points,2,-10,10));
 assert(!route_follow_update(&r,40,-74,0,123));
 puts("RouteFollow 16k-point / 4k-fix sanitizer stress passed");return 0;
}
