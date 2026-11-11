#ifndef GPS_FOLLOW_GATE_H
#define GPS_FOLLOW_GATE_H
#include <math.h>
/* Adaptive GPS-follow camera gate. Suppresses stationary jitter, not GPS fixes. */
static inline int gps_should_move_camera(double current_lat,double current_lon,
    double fix_lat,double fix_lon,double accuracy_m,unsigned age_ms,int zoom){
    if(!isfinite(current_lat)||!isfinite(current_lon)||!isfinite(fix_lat)||
       !isfinite(fix_lon)||fabs(fix_lat)>90.0||fabs(fix_lon)>180.0||
       age_ms>7000||zoom<0||zoom>18)return 0;
    const double rad=0.017453292519943295;
    double dlat=(fix_lat-current_lat)*rad;
    double dlon=(fix_lon-current_lon)*rad;
    if(dlon>3.141592653589793)dlon-=6.283185307179586;
    if(dlon< -3.141592653589793)dlon+=6.283185307179586;
    double v=sin(dlat*.5)*sin(dlat*.5)+cos(current_lat*rad)*cos(fix_lat*rad)*sin(dlon*.5)*sin(dlon*.5);
    if(v>1)v=1;
    if(v<0)v=0;
    double meters=12742008.8*atan2(sqrt(v),sqrt(1.0-v));
    double meters_per_pixel=156543.0339*fmax(.05,cos(fix_lat*rad))/(1<<zoom);
    double accuracy=(isfinite(accuracy_m)&&accuracy_m>=0)?accuracy_m:6.0;
    double deadband=fmax(4.0,fmin(20.0,fmax(.50*meters_per_pixel,.30*accuracy)));
    return meters>=deadband;
}
#endif
