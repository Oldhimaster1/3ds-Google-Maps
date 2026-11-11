#ifndef STREET_NAV_H
#define STREET_NAV_H
#include "panorama_live.h"

/* Pure navigation math shared by L/R, touch and host regression tests. */
float street_nav_bearing(double from_lat, double from_lon,
                         double to_lat, double to_lon);
float street_nav_delta(float target, float heading);
int street_nav_forward(const PanoLiveImage *p, float view_yaw_rad);
int street_nav_backward(const PanoLiveImage *p);
/* 0..8 cells, arranged 3x3; centre (4) has no destination. */
int street_nav_at_cell(const PanoLiveImage *p, float view_yaw_rad, int cell);
int street_nav_cell_for_bearing(float bearing, float absolute_heading);
#endif
