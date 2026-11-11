#include "pano_gyro.h"
#include <math.h>
#include <string.h>

void pano_gyro_reset(PanoGyroFilter *filter) {
    if (filter) memset(filter, 0, sizeof(*filter));
}

static float clampf(float value, float low, float high) {
    return fmaxf(low, fminf(high, value));
}

bool pano_gyro_step(PanoGyroFilter *filter, float raw_pitch, float raw_yaw,
                    float counts_per_dps, float dt, bool invert,
                    float *pitch_rad, float *yaw_rad) {
    if (!filter || !pitch_rad || !yaw_rad ||
        !isfinite(raw_pitch) || !isfinite(raw_yaw) ||
        !isfinite(counts_per_dps) || counts_per_dps < .05f ||
        counts_per_dps > 2000.0f ||
        !isfinite(dt) || dt <= 0 || dt > .14f)
        return false;

    /* 3DS HID returns the raw angular velocity in sensor counts.
     * r = coefficient * degrees/second, not the reverse. */
    float pitch_dps = raw_pitch / counts_per_dps;
    float yaw_dps = raw_yaw / counts_per_dps;
    if (!isfinite(pitch_dps) || !isfinite(yaw_dps) ||
        fabsf(pitch_dps)>500 || fabsf(yaw_dps)>500) return filter->ready;

    /* Require ~0.7 seconds of stationary samples at 60 fps. Earlier 20
     * samples frequently captured hand movement and taught the filter
     * an incorrect baseline, causing enormous apparent drift. */
    if (!filter->ready) {
        if (filter->samples >= 5 &&
            (fabsf(pitch_dps-filter->pitch_sum/filter->samples)>7.0f ||
             fabsf(yaw_dps-filter->yaw_sum/filter->samples)>7.0f)) {
            pano_gyro_reset(filter);
            return false;
        }
        filter->pitch_sum += pitch_dps;
        filter->yaw_sum += yaw_dps;
        if (++filter->samples >= 45) {
            filter->pitch_bias=filter->pitch_sum/filter->samples;
            filter->yaw_bias=filter->yaw_sum/filter->samples;
            filter->ready=true;
        }
        return filter->ready;
    }

    float pitch=pitch_dps-filter->pitch_bias;
    float yaw=yaw_dps-filter->yaw_bias;
    /* On a handheld original 3DS, physical yaw can leak into pitch and
     * vice versa. Suppress small cross-axis sensor readings when one axis
     * overwhelmingly dominates; preserve deliberate diagonal movement. */
    if (fabsf(yaw)>fabsf(pitch)*2.3f && fabsf(pitch)<10.0f) pitch=0;
    else if (fabsf(pitch)>fabsf(yaw)*2.3f && fabsf(yaw)<10.0f) yaw=0;

    const float noise=2.3f;
    bool still=fabsf(pitch)<noise && fabsf(yaw)<noise;
    if (still) {
        /* Stop immediately when physically stationary: filtered angular
         * velocity must not keep spinning the camera after the user stops. */
        filter->pitch_velocity=0;
        filter->yaw_velocity=0;
        const float a=clampf(dt*.07f,0,.01f);
        filter->pitch_bias+=(pitch_dps-filter->pitch_bias)*a;
        filter->yaw_bias+=(yaw_dps-filter->yaw_bias)*a;
        return true;
    }

    float target_pitch=fabsf(pitch)>noise ? clampf(pitch,-120,120) : 0;
    float target_yaw=fabsf(yaw)>noise ? clampf(yaw,-120,120) : 0;
    float elapsed=fminf(dt,.033f);
    float alpha=elapsed/(elapsed+.07f);
    filter->pitch_velocity += alpha*(target_pitch-filter->pitch_velocity);
    filter->yaw_velocity += alpha*(target_yaw-filter->yaw_velocity);
    const float sign=invert ? -1.0f : 1.0f;
    const float radians=.017453292519943295f;
    *pitch_rad+=sign*filter->pitch_velocity*elapsed*radians;
    *yaw_rad+=sign*filter->yaw_velocity*elapsed*radians;
    return true;
}
