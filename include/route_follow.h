#ifndef MAP_ROUTE_FOLLOW_H
#define MAP_ROUTE_FOLLOW_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {double lat,lon;} RouteFollowPoint;
typedef struct {
    const RouteFollowPoint *path;
    size_t count, last_segment;
    double *metres;
    double geometry_m, route_m, duration_s;
    double progress_m, remaining_m, eta_s, cross_track_m;
    bool matched,off_route;
    unsigned off_route_fixes;
    uint64_t last_reroute_ms;
} RouteFollow;
bool route_follow_begin(RouteFollow *r,const RouteFollowPoint *path,size_t count,
                        double official_metres,double duration_seconds);
void route_follow_reset(RouteFollow *r);
bool route_follow_update(RouteFollow *r,double lat,double lon,double accuracy_m,
                         uint64_t now_ms);
bool route_follow_needs_reroute(RouteFollow *r,uint64_t now_ms);
#endif
