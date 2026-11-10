#include "gyro_stable.h"
#include <math.h>
#include <string.h>

#define DEG_TO_RAD 0.01745329251994329577f
/* The calibration requires the user to keep the console still for ~0.6 s.
 * If motion is observed, restart rather than treating motion as bias. */
void gyro_stable_reset(GyroStable *gyro) {
    if (gyro) memset(gyro, 0, sizeof(*gyro));
}

static float clampf(float x, float lo, float hi) {
    return fmaxf(lo, fminf(hi, x));
}

void gyro_stable_step(GyroStable *g, float raw_pitch, float raw_yaw,
                      float coeff, float dt, bool invert,
                      float *pitch, float *yaw) {
    if (!g || !pitch || !yaw || !isfinite(raw_pitch) || !isfinite(raw_yaw) ||
        !isfinite(coeff) || coeff <= 0 || coeff > 20 ||
        !isfinite(dt) || dt <= 0 || dt > .075f) return;
    float p = raw_pitch * coeff, y = raw_yaw * coeff;
    if (!isfinite(p) || !isfinite(y) || fabsf(p) > 650 || fabsf(y) > 650)
        return; /* Ignore corrupt calibration and impossible spikes. */

    if (!g->ready) {
        if (g->samples >= 6 &&
            (fabsf(p - g->sum_pitch / g->samples) > 22 ||
             fabsf(y - g->sum_yaw / g->samples) > 22)) {
            gyro_stable_reset(g); /* Moved during calibration. */
        }
        g->sum_pitch += p;
        g->sum_yaw += y;
        if (++g->samples >= 38) {
            g->bias_pitch = g->sum_pitch / g->samples;
            g->bias_yaw = g->sum_yaw / g->samples;
            g->ready = true;
        }
        return;
    }

    p -= g->bias_pitch;
    y -= g->bias_yaw;
    /* Stationary bias correction is deliberately slow (25-second time
     * constant), and applies only near rest, not during deliberate panning. */
    if (fabsf(p) < 2.0f && fabsf(y) < 2.0f) {
        float a = clampf(dt / 25.f, 0.f, .02f);
        g->bias_pitch += p * a;
        g->bias_yaw += y * a;
    }

    /* First-order low-pass with a ~100 ms time constant, independent of FPS. */
    float a = 1.0f - expf(-dt / .10f);
    g->smooth_pitch += a * (clampf(p, -240.f, 240.f) - g->smooth_pitch);
    g->smooth_yaw += a * (clampf(y, -240.f, 240.f) - g->smooth_yaw);
    /* Small sensor jitter must not become perpetual camera drift. */
    float vp = fabsf(g->smooth_pitch) < 2.0f ? 0 : g->smooth_pitch;
    float vy = fabsf(g->smooth_yaw) < 2.0f ? 0 : g->smooth_yaw;
    const float sign = invert ? -1.f : 1.f;
    const float sensitivity = .65f;
    *pitch += clampf(sign * vp * sensitivity * dt * DEG_TO_RAD, -.035f, .035f);
    *yaw   += clampf(sign * vy * sensitivity * dt * DEG_TO_RAD, -.035f, .035f);
}
