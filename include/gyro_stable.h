#ifndef GYRO_STABLE_H
#define GYRO_STABLE_H
#include <stdbool.h>
typedef struct {
    float bias_pitch, bias_yaw;
    float sum_pitch, sum_yaw;
    float smooth_pitch, smooth_yaw;
    int samples;
    bool ready;
} GyroStable;
void gyro_stable_reset(GyroStable *gyro);
void gyro_stable_step(GyroStable *gyro, float raw_pitch, float raw_yaw,
                      float dps_per_raw, float dt, bool invert,
                      float *pitch, float *yaw);
#endif
