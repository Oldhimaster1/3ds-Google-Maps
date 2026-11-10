#include "pano_gyro.h"
#include <math.h>
#include <string.h>

void pano_gyro_reset(PanoGyroFilter *filter) {
    if (filter) memset(filter, 0, sizeof(*filter));
}

static float clampf(float v, float low, float high) {
    return fmaxf(low, fminf(high, v));
}

bool pano_gyro_step(PanoGyroFilter *filter, float raw_pitch, float raw_yaw,
                    float counts_per_dps, float dt, bool invert,
                    float *pitch_rad, float *yaw_rad) {
    if (!filter || !pitch_rad || !yaw_rad || !isfinite(raw_pitch) ||
        !isfinite(raw_yaw) || !isfinite(counts_per_dps) ||
        counts_per_dps < 0.05f || counts_per_dps > 2000.0f ||
        !isfinite(dt) || dt <= 0 || dt > 0.14f) return false;
    /* Critical: HID raw count = degrees/second * coefficient, not the
     * reverse. The previous multiply made a normal 90 dps turn exceed
     * 18,000 dps when the 3DS returned its usual ~14.375 coefficient. */
    const float p = raw_pitch / counts_per_dps;
    const float y = raw_yaw / counts_per_dps;
    if (!isfinite(p) || !isfinite(y) || fabsf(p) > 1500 || fabsf(y) > 1500)
        return filter->ready;
    /* 20 frames is about one third-second at 60fps. Hold still to remove
     * hardware zero drift; do not let calibration itself rotate the camera. */
    if (!filter->ready) {
        if (filter->samples >= 6 &&
            (fabsf(p - filter->pitch_sum / filter->samples) > 22 ||
             fabsf(y - filter->yaw_sum / filter->samples) > 22))
            pano_gyro_reset(filter); /* Console moved during zeroing. */
        filter->pitch_sum += p;
        filter->yaw_sum += y;
        if (++filter->samples >= 20) {
            filter->pitch_bias = filter->pitch_sum / filter->samples;
            filter->yaw_bias = filter->yaw_sum / filter->samples;
            filter->ready = true;
        }
        return filter->ready;
    }
    float pv = p - filter->pitch_bias;
    float yv = y - filter->yaw_bias;
    /* Slowly adapt bias only while the sensor appears stationary. */
    if (fabsf(pv) < 1.7f && fabsf(yv) < 1.7f) {
        float adapt = clampf(dt * 0.18f, 0.0f, 0.02f);
        filter->pitch_bias += pv * adapt;
        filter->yaw_bias += yv * adapt;
    }
    /* Low-pass angular rates before integration. No abrupt snap-back. */
    float elapsed = fminf(dt, 0.05f);
    float alpha = 1.0f - expf(-elapsed / 0.10f);
    float wanted_p = fabsf(pv) > 1.50f ? clampf(pv, -180, 180) : 0;
    float wanted_y = fabsf(yv) > 1.50f ? clampf(yv, -180, 180) : 0;
    filter->pitch_velocity += alpha * (wanted_p - filter->pitch_velocity);
    filter->yaw_velocity += alpha * (wanted_y - filter->yaw_velocity);
    if (fabsf(filter->pitch_velocity) < 0.12f) filter->pitch_velocity = 0;
    if (fabsf(filter->yaw_velocity) < 0.12f) filter->yaw_velocity = 0;
    float sign = invert ? -1 : 1;
    const float rad = 0.017453292519943295f;
    /* Cap per-frame deltas: a sensor spike must never swing the world. */
    *pitch_rad += clampf(sign * filter->pitch_velocity * elapsed * rad * .65f, -.035f, .035f);
    *yaw_rad += clampf(sign * filter->yaw_velocity * elapsed * rad * .65f, -.035f, .035f);
    return true;
}
