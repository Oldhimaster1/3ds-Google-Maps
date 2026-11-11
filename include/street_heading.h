#ifndef STREET_HEADING_H
#define STREET_HEADING_H
#include <stdbool.h>
/* Converts the outgoing panorama's relative camera orientation to the new
 * panorama's reference heading; all angles are in radians/degrees as named. */
bool street_heading_transfer(float old_azimuth_deg,float old_yaw_rad,
                             float new_azimuth_deg,float *new_yaw_rad);
#endif
