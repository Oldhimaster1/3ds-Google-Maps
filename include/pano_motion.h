#ifndef PANO_MOTION_H
#define PANO_MOTION_H
#include <stdbool.h>
/* Maps the 3DS device's angular velocities into look motion for a handheld
 * console at any reasonable angle, without mistaking roll for a camera tilt. */
typedef struct {
    float gx,gy,gz;  /* smoothed gravity in device coordinates */
    bool gravity_ready;
} PanoMotionFrame;
void pano_motion_reset(PanoMotionFrame *m);
/* Gyro rates can be raw counts (scale conversion is done later). Inputs
 * from hidGyroRead and hidAccelRead use their native x,y,z orders. */
bool pano_motion_project(PanoMotionFrame *m,
    float wx,float wy,float wz,float ax,float ay,float az,
    float *pitch_rate,float *yaw_rate);
#endif