#include "pano_motion.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
static int closef(float x,float y){return fabsf(x-y)<0.001f;}
int main(void){
 PanoMotionFrame m={0};float p=0,y=0;
 /* Flat console: turning around Z changes yaw only. */
 assert(pano_motion_project(&m,0,0,100,0,0,500,&p,&y));
 assert(closef(p,0)&&closef(y,100));
 assert(pano_motion_project(&m,100,0,0,0,0,500,&p,&y));
 assert(closef(p,100)&&closef(y,0));
 assert(pano_motion_project(&m,0,100,0,0,0,500,&p,&y));
 assert(closef(p,0)&&closef(y,0));
 /* Vertical grip: world yaw is now device Y, with no pitch coupling. */
 pano_motion_reset(&m);
 assert(pano_motion_project(&m,0,100,0,0,500,0,&p,&y));
 assert(closef(p,0)&&closef(y,100));
 assert(pano_motion_project(&m,100,0,0,0,500,0,&p,&y));
 assert(closef(p,100)&&closef(y,0));
 /* Invalid accelerometer data falls back without losing yaw. */
 pano_motion_reset(&m);
 assert(pano_motion_project(&m,0,0,50,0,0,0,&p,&y));
 assert(closef(y,50)&&closef(p,0));
 assert(!pano_motion_project(&m,NAN,0,0,0,0,0,&p,&y));
 puts("Pano gravity-aware axis tests passed");return 0;
}