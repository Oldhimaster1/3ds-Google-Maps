#include "pano_gyro.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

/* libctru/Citra: raw counts = degrees per second * coefficient. The Old 3DS
 * sensor coefficient is typically close to 14.375, not 1/14.375. */
static const float counts_per_dps = 14.375f;

int main(void) {
    PanoGyroFilter f;
    pano_gyro_reset(&f);
    float pitch = 0, yaw = 0;
    for (int i = 0; i < 30; i++)
        pano_gyro_step(&f, 58, 72, counts_per_dps, .016f,
                       true, &pitch, &yaw);
    assert(f.ready);
    assert(pitch == 0 && yaw == 0);
    /* 30k stationary frames with sub-degree/sec raw noise must not spin. */
    for (int i = 0; i < 30000; i++)
        pano_gyro_step(&f, 58 + i % 7 - 3, 72 + i % 9 - 4,
                       counts_per_dps, .016f, true, &pitch, &yaw);
    assert(fabsf(pitch) < .02f && fabsf(yaw) < .02f);
    /* 90°/second physical motion must produce bounded normal rotation,
     * not thousands of degrees/second from incorrect multiplication. */
    for (int i = 0; i < 60; i++)
        pano_gyro_step(&f, 58 + 90 * counts_per_dps,
                       72 + 90 * counts_per_dps, counts_per_dps,
                       .016f, true, &pitch, &yaw);
    assert(pitch < -.3f && pitch > -2.0f);
    assert(yaw < -.3f && yaw > -2.0f);
    float old_yaw = yaw;
    pano_gyro_step(&f, 30000, -30000, counts_per_dps,
                   .016f, true, &pitch, &yaw);
    assert(yaw == old_yaw); /* reject physically impossible spike */
    pano_gyro_reset(&f);
    pitch = yaw = 0;
    for (int i = 0; i < 30; i++)
        pano_gyro_step(&f, 0, 0, counts_per_dps, .016f, false, &pitch, &yaw);
    for (int i = 0; i < 60; i++)
        pano_gyro_step(&f, 90 * counts_per_dps, 90 * counts_per_dps,
                       counts_per_dps, .016f, false, &pitch, &yaw);
    assert(pitch > .3f && yaw > .3f);
    puts("PASS: correct Old 3DS raw coefficient, 30k-frame drift, low-pass, spikes and axes");
    return 0;
}
