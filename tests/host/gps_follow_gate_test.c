#include "gps_follow_gate.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    assert(!gps_should_move_camera(40.0,-74.0,40.0,-74.0,5,0,16));
    assert(!gps_should_move_camera(40.0,-74.0,40.000003,-74.000004,9,0,16));
    assert(gps_should_move_camera(40.0,-74.0,40.001,-74.0,8,0,16));
    assert(!gps_should_move_camera(40.0,-74.0,40.001,-74.0,8,9001,16));
    assert(gps_should_move_camera(0,179.99,0,-179.99,10,1,18));
    assert(!gps_should_move_camera(1,1,NAN,5,1,0,16));
    assert(!gps_should_move_camera(1,1,1,1,1,1,20));
    puts("GPS-follow jitter gate and dateline tests passed");return 0;
}
