#ifndef PANO_GYRO_H
#define PANO_GYRO_H
#include <stdbool.h>
typedef struct {
    float pitch_bias, yaw_bias;
    float pitch_velocity, yaw_velocity;
    float pitch_sum, yaw_sum;
    unsigned samples;
    bool ready;
} PanoGyroFilter;
/* Re-zero with device held still when enabling or pressing Select. */
void pano_gyro_reset(PanoGyroFilter *filter);
/* Raw 3DS gyro counts are approximately coeff * degrees_per_second.
 * Return true once the initial stationary bias has been estimated. */
bool pano_gyro_step(PanoGyroFilter *filter, float raw_pitch, float raw_yaw,
                    float counts_per_dps, float dt, bool invert,
                    float *pitch_rad, float *yaw_rad);
#endif
