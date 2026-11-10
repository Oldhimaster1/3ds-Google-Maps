#include "street_nav.h"
#include <math.h>
#include <string.h>

#define NAV_PI 3.14159265358979323846

float street_nav_bearing(double lat1, double lon1, double lat2, double lon2) {
    const double r = NAV_PI / 180.0;
    const double a = lat1 * r, b = lat2 * r;
    const double d = (lon2 - lon1) * r;
    double y = sin(d) * cos(b);
    double x = cos(a)*sin(b) - sin(a)*cos(b)*cos(d);
    double n = atan2(y, x) * 180.0 / NAV_PI;
    if (n < 0.0) n += 360.0;
    return (float)n;
}

float street_nav_delta(float bearing, float heading) {
    float d = remainderf(bearing - heading, 360.f);
    return isfinite(d) ? d : 0.f;
}

int street_nav_forward(const PanoLiveImage *p, float yaw_rad) {
    if (!p) return -1;
    float heading = p->azimuth + yaw_rad * (180.f / (float)NAV_PI);
    int best = -1;
    float score = 181.f;
    for (unsigned i = 0; i < p->link_count && i < PANO_LINK_MAX; ++i) {
        if (!p->links[i].id[0]) continue;
        float d = fabsf(street_nav_delta(p->links[i].bearing, heading));
        if (d < score) { score = d; best = (int)i; }
    }
    /* An intersection behind the camera isn't a plausible forward choice. */
    return score <= 100.f ? best : -1;
}

int street_nav_backward(const PanoLiveImage *p) {
    if (!p) return -1;
    int best = -1;
    float score = 181.f;
    for (unsigned i = 0; i < p->link_count && i < PANO_LINK_MAX; ++i) {
        if (!p->links[i].id[0]) continue;
        float d = fabsf(street_nav_delta(p->links[i].bearing, p->azimuth + 180.f));
        if (d < score) { score = d; best = (int)i; }
    }
    return best;
}

int street_nav_cell_for_bearing(float bearing, float heading) {
    float delta = street_nav_delta(bearing, heading);
    /* East/clockwise is positive; y is downward on the lower screen. */
    int sector = (int)floorf((delta + 22.5f) / 45.f);
    sector = (sector % 8 + 8) % 8;
    static const int cells[8] = {1,2,5,8,7,6,3,0};
    return cells[sector];
}

int street_nav_at_cell(const PanoLiveImage *p, float yaw_rad, int cell) {
    if (!p || cell < 0 || cell >= 9 || cell == 4) return -1;
    float heading = p->azimuth + yaw_rad * (180.f / (float)NAV_PI);
    int best = -1;
    float best_error = 181.f;
    float sector_angle = 0.f;
    switch (cell) {
      case 1: sector_angle = 0; break;
      case 2: sector_angle = 45; break;
      case 5: sector_angle = 90; break;
      case 8: sector_angle = 135; break;
      case 7: sector_angle = 180; break;
      case 6: sector_angle = -135; break;
      case 3: sector_angle = -90; break;
      case 0: sector_angle = -45; break;
      default: return -1;
    }
    for (unsigned i = 0; i < p->link_count && i < PANO_LINK_MAX; i++) {
        if (p->links[i].id[0] && street_nav_cell_for_bearing(p->links[i].bearing, heading) == cell) {
            float error = fabsf(street_nav_delta(p->links[i].bearing, heading + sector_angle));
            if (error < best_error) { best = (int)i; best_error = error; }
        }
    }
    return best;
}
