#ifndef GPS_MOTION_H
#define GPS_MOTION_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    double lat,lon,speed_mps,heading_deg,accuracy_m;
    uint64_t tick_ms;
    bool has_fix,has_speed,has_heading;
} GpsMotion;
void gps_motion_reset(GpsMotion *m);
bool gps_motion_update(GpsMotion *m,double lat,double lon,double accuracy_m,
                       double speed_mps,double heading_deg,uint64_t tick_ms);
#endif
