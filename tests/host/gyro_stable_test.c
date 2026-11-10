#include "gyro_stable.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void) {
    GyroStable s;
    gyro_stable_reset(&s);
    float pitch=0,yaw=0;
    for (int i=0;i<40;i++)
        gyro_stable_step(&s, 40, 55, .1f, .016f, true, &pitch, &yaw);
    assert(s.ready);
    assert(pitch==0 && yaw==0); /* calibrated rest must not spin */
    for (int i=0;i<1000;i++)
        gyro_stable_step(&s, 40, 55, .1f, .016f, true, &pitch, &yaw);
    assert(fabsf(pitch)<.0001f && fabsf(yaw)<.0001f);
    for (int i=0;i<60;i++)
        gyro_stable_step(&s, 200, 400, .1f, .016f, true, &pitch, &yaw);
    assert(pitch<0 && yaw<0);
    float old_yaw=yaw;
    gyro_stable_step(&s, 10000, 9000, .1f, .016f, true, &pitch, &yaw);
    assert(yaw==old_yaw); /* rejects spike */
    gyro_stable_reset(&s); pitch=yaw=0;
    for (int i=0;i<40;i++)
        gyro_stable_step(&s, 0, 0, .1f, .016f, false, &pitch, &yaw);
    for (int i=0;i<60;i++)
        gyro_stable_step(&s, 200, 400, .1f, .016f, false, &pitch, &yaw);
    assert(pitch>0 && yaw>0);
    puts("PASS: stationary bias, smoothing, spike rejection, direction inversion");
    return 0;
}
