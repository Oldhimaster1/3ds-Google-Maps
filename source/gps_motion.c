#include "gps_motion.h"
#include <math.h>
#include <string.h>
static double rad(double x){return x*.017453292519943295;}
static double norm(double h){h=fmod(h,360.);return h<0?h+360.:h;}
void gps_motion_reset(GpsMotion *m){if(m)memset(m,0,sizeof(*m));}
bool gps_motion_update(GpsMotion *m,double lat,double lon,double acc,
                       double speed,double heading,uint64_t tick){
    if(!m||!isfinite(lat)||!isfinite(lon)||fabs(lat)>90||fabs(lon)>180 ||
       !isfinite(acc)||acc<0||acc>100000)return false;
    bool speed_good=isfinite(speed)&&speed>=0&&speed<110;
    bool heading_good=isfinite(heading)&&heading>=0&&heading<360;
    double estimated_speed=NAN,estimated_heading=NAN;
    if(m->has_fix&&tick>m->tick_ms){
        double dt=(tick-m->tick_ms)*.001;
        if(dt>=.4&&dt<=20){
            double y=sin(rad(lon-m->lon))*cos(rad(lat));
            double x=cos(rad(m->lat))*sin(rad(lat))-
                sin(rad(m->lat))*cos(rad(lat))*cos(rad(lon-m->lon));
            double a=sin(rad(lat-m->lat)/2)*sin(rad(lat-m->lat)/2)+
                cos(rad(m->lat))*cos(rad(lat))*
                sin(rad(lon-m->lon)/2)*sin(rad(lon-m->lon)/2);
            a=fmax(0.,fmin(1.,a));
            double dist=12742008.8*atan2(sqrt(a),sqrt(1-a));
            /* Don't infer speed/heading from position noise smaller than
               accuracy of either fix, or from unrealistic teleportation. */
            if(dist>fmax(6.,fmax(acc,m->accuracy_m)*1.5)&&dist/dt<110){
                estimated_speed=dist/dt;
                estimated_heading=norm(atan2(y,x)*180./3.141592653589793);
            }
        }
    }
    m->lat=lat;m->lon=lon;m->accuracy_m=acc;m->tick_ms=tick;m->has_fix=true;
    if(!speed_good&&isfinite(estimated_speed)){speed=estimated_speed;speed_good=true;}
    if(!heading_good&&isfinite(estimated_heading)){heading=estimated_heading;heading_good=true;}
    if(speed_good){m->speed_mps=speed;m->has_speed=true;}
    else {m->speed_mps=0;m->has_speed=false;}
    /* GNSS course is unreliable when stationary. Hide it instead of showing
       a random compass arrow. */
    if(heading_good&&speed_good&&speed>=.8){
        m->heading_deg=norm(heading);m->has_heading=true;
    }else m->has_heading=false;
    return true;
}
