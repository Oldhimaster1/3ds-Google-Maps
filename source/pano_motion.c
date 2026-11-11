#include "pano_motion.h"
#include <math.h>
#include <string.h>
void pano_motion_reset(PanoMotionFrame *m){if(m)memset(m,0,sizeof(*m));}
static bool finite3(float a,float b,float c){return isfinite(a)&&isfinite(b)&&isfinite(c);}
bool pano_motion_project(PanoMotionFrame *m,float wx,float wy,float wz,
 float ax,float ay,float az,float *pitch,float *yaw){
    if(!m||!pitch||!yaw||!finite3(wx,wy,wz))return false;
    /* Without acceleration data use hinge X for pitch and HID yaw Z.
     * This fallback is intentional and never routes yaw to pitch. */
    float gx=0,gy=0,gz=1;
    if(finite3(ax,ay,az)){
        float mag=sqrtf(ax*ax+ay*ay+az*az);
        if(mag>0.5f&&mag<5000.0f){
            float vx=ax/mag,vy=ay/mag,vz=az/mag;
            if(!m->gravity_ready){m->gx=vx;m->gy=vy;m->gz=vz;m->gravity_ready=true;}
            else {
                /* Filter hand jitter while following slow changes in grip. */
                const float rate=.14f;
                m->gx+=(vx-m->gx)*rate;
                m->gy+=(vy-m->gy)*rate;
                m->gz+=(vz-m->gz)*rate;
                float norm=sqrtf(m->gx*m->gx+m->gy*m->gy+m->gz*m->gz);
                if(norm>.2f){m->gx/=norm;m->gy/=norm;m->gz/=norm;}
            }
        }
    }
    if(m->gravity_ready){gx=m->gx;gy=m->gy;gz=m->gz;}
    /* Yaw is rotation around gravity, not blindly around sensor Z. */
    *yaw=wx*gx+wy*gy+wz*gz;
    /* Pitch is the console's hinge X axis with its vertical component
     * removed. Prevents a left/right turn from changing look elevation. */
    float hx=1-gx*gx,hy=-gx*gy,hz=-gx*gz;
    float hl=sqrtf(hx*hx+hy*hy+hz*hz);
    if(hl<.20f){
        /* Extreme sideways grip: project Y instead of an unstable X. */
        hx=-gy*gx;hy=1-gy*gy;hz=-gy*gz;
        hl=sqrtf(hx*hx+hy*hy+hz*hz);
    }
    if(hl<.20f){*pitch=0;return true;}
    *pitch=(wx*hx+wy*hy+wz*hz)/hl;
    return finite3(*pitch,*yaw,hl);
}