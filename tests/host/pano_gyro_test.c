#include "pano_gyro.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static float radians(float degrees) { return degrees*.017453292519943295f; }
int main(void) {
    PanoGyroFilter g={0};
    float pitch=0,yaw=0;
    pano_gyro_reset(&g);
    for(int i=0;i<45;i++)
        pano_gyro_step(&g,14.375f*1.0f,-14.375f*.4f,
                       14.375f,.016f,false,&pitch,&yaw);
    assert(g.ready);
    assert(yaw==0 && pitch==0);
    for(int i=0;i<110;i++)
        pano_gyro_step(&g,14.375f*1.0f,14.375f*60.f,
                       14.375f,.016f,false,&pitch,&yaw);
    assert(yaw > radians(90) && yaw < radians(130));
    assert(fabsf(pitch) < .04f);
    float before=yaw;
    for(int i=0;i<450;i++)
        pano_gyro_step(&g,14.375f*1.0f,14.375f*.4f,
                       14.375f,.016f,false,&pitch,&yaw);
    assert(fabsf(yaw-before) < radians(8));
    pano_gyro_reset(&g);
    for(int i=0;i<45;i++)
        pano_gyro_step(&g,0,0,14.375f,.016f,true,&pitch,&yaw);
    float old=yaw;
    for(int i=0;i<90;i++)
        pano_gyro_step(&g,0,14.375f*45.f,14.375f,.016f,true,&pitch,&yaw);
    assert(yaw < old-radians(50));
    assert(!pano_gyro_step(&g,0,1,NAN,.016f,true,&pitch,&yaw));
    /* Calibration must reject picking up and moving the console.
     * A malformed initial zero-bias estimate previously drove bad drift. */
    pano_gyro_reset(&g);
    for (int i=0; i<10; i++)
        pano_gyro_step(&g,0,0,14.375f,.016f,false,&pitch,&yaw);
    assert(!pano_gyro_step(&g,0,14.375f*75,14.375f,.016f,
                           false,&pitch,&yaw));
    assert(!g.ready && g.samples < 10);
    /* After stationary samples, there must be no residual turn. */
    pano_gyro_reset(&g);
    for (int i=0; i<45; i++)
        pano_gyro_step(&g,0,0,14.375f,.016f,false,&pitch,&yaw);
    for (int i=0; i<50; i++)
        pano_gyro_step(&g,0,14.375f*45,14.375f,.016f,false,&pitch,&yaw);
    float stopped=yaw;
    pano_gyro_step(&g,0,0,14.375f,.016f,false,&pitch,&yaw);
    assert(yaw==stopped);

    puts("PASS 3DS gyro 14.375 counts/dps calibration, smooth turn, stillness, invert, invalid inputs");
    return 0;
}
