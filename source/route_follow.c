#include "route_follow.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define RAD (0.0174532925199432957692)
#define EARTH_M 6371008.8
static double wrap_delta(double lon){return remainder(lon,360.);}
static bool valid(double lat,double lon){return isfinite(lat)&&isfinite(lon)&&fabs(lat)<=90&&fabs(lon)<=180;}
static double segment_length(RouteFollowPoint a,RouteFollowPoint b){
    double a1=a.lat*RAD,a2=b.lat*RAD;
    double dlat=(b.lat-a.lat)*RAD,dlon=wrap_delta(b.lon-a.lon)*RAD;
    double h=sin(dlat*.5)*sin(dlat*.5)+cos(a1)*cos(a2)*sin(dlon*.5)*sin(dlon*.5);
    return 2*EARTH_M*asin(sqrt(fmax(0.,fmin(1.,h))));
}
void route_follow_reset(RouteFollow *r){if(!r)return;free(r->metres);memset(r,0,sizeof(*r));}
bool route_follow_begin(RouteFollow *r,const RouteFollowPoint *path,size_t count,
                        double official_metres,double duration_seconds){
    if(!r||!path||count<2||count>16000||
       !isfinite(official_metres)||official_metres<0||
       !isfinite(duration_seconds)||duration_seconds<0)return false;
    double *cum=calloc(count,sizeof(*cum));if(!cum)return false;
    for(size_t i=1;i<count;i++){
        if(!valid(path[i-1].lat,path[i-1].lon)||!valid(path[i].lat,path[i].lon)){free(cum);return false;}
        double d=segment_length(path[i-1],path[i]);
        if(!isfinite(d)||d>1000000){free(cum);return false;}
        cum[i]=cum[i-1]+d;
    }
    if(cum[count-1]<1){free(cum);return false;}
    route_follow_reset(r);
    r->path=path;r->count=count;r->metres=cum;r->geometry_m=cum[count-1];
    r->route_m=official_metres>0?official_metres:r->geometry_m;
    r->duration_s=duration_seconds;
    r->remaining_m=r->route_m;r->eta_s=duration_seconds;
    return true;
}
static void nearest(const RouteFollow *r,double lat,double lon,size_t start,size_t end,
                    double *best,size_t *segment,double *fraction){
    double c=cos(lat*RAD);
    for(size_t i=start;i<end;i++){
        RouteFollowPoint a=r->path[i],b=r->path[i+1];
        double ax=wrap_delta(a.lon-lon)*111195.*c,ay=(a.lat-lat)*111195.;
        double bx=wrap_delta(b.lon-lon)*111195.*c,by=(b.lat-lat)*111195.;
        double vx=bx-ax,vy=by-ay,denom=vx*vx+vy*vy;
        double t=denom>1e-8?fmax(0.,fmin(1.,-(ax*vx+ay*vy)/denom)):0.;
        double dx=ax+t*vx,dy=ay+t*vy,d2=dx*dx+dy*dy;
        if(r->matched && i+25<r->last_segment)d2+=100.;
        if(d2<*best){*best=d2;*segment=i;*fraction=t;}
    }
}
bool route_follow_update(RouteFollow *r,double lat,double lon,double accuracy_m,uint64_t now_ms){
    (void)now_ms;
    if(!r||!r->metres||!valid(lat,lon)||!isfinite(accuracy_m)||accuracy_m<0||
       accuracy_m>120)return false;
    double best=INFINITY,fraction=0;size_t seg=0;
    if(r->matched){
        size_t begin=r->last_segment>48?r->last_segment-48:0;
        size_t end=r->last_segment+49<r->count-1?r->last_segment+49:r->count-1;
        nearest(r,lat,lon,begin,end,&best,&seg,&fraction);
    }
    if(!r->matched || best>fmax(150.,accuracy_m*4)*fmax(150.,accuracy_m*4))
        nearest(r,lat,lon,0,r->count-1,&best,&seg,&fraction);
    double along=r->metres[seg]+fraction*(r->metres[seg+1]-r->metres[seg]);
    r->last_segment=seg;r->matched=true;
    r->progress_m=r->route_m*fmin(1.,along/r->geometry_m);
    r->remaining_m=fmax(0.,r->route_m-r->progress_m);
    r->eta_s=r->duration_s*(r->remaining_m/r->route_m);
    r->cross_track_m=sqrt(best);
    double threshold=fmax(40.,accuracy_m*2.5);
    r->off_route=r->cross_track_m>threshold;
    if(r->off_route){if(r->off_route_fixes<100)r->off_route_fixes++;}
    else r->off_route_fixes=0;
    return true;
}
bool route_follow_needs_reroute(RouteFollow *r,uint64_t now_ms){
    if(!r||!r->matched||!r->off_route||r->off_route_fixes<3)return false;
    if(r->last_reroute_ms && (now_ms<r->last_reroute_ms ||
                             now_ms-r->last_reroute_ms<30000))return false;
    r->last_reroute_ms=now_ms;r->off_route_fixes=0;
    return true;
}
