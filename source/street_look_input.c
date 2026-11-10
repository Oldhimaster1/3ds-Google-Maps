#include "street_look_input.h"
#include <math.h>
#include <stdlib.h>

static float axis(int raw) {
    const float deadzone=19.0f;
    float absolute=fabsf((float)raw);
    if (absolute<=deadzone) return 0;
    float value=fminf(1.0f,(absolute-deadzone)/(156.0f-deadzone));
    /* Nonlinear near-center input permits fine aiming without drift. */
    value=value*value*(1.45f-0.45f*value);
    return raw<0?-value:value;
}
void street_look_axes(int raw_x, int raw_y, float *turn_x, float *turn_y) {
    if (!turn_x || !turn_y) return;
    float x=axis(raw_x), y=axis(raw_y);
    /* When the user predominantly pans horizontally, suppress vertical
     * analog cross-talk. Deliberate diagonals still support simultaneous
     * yaw + pitch. This does not affect globe/terrain camera controls. */
    if (abs(raw_x)>42 && abs(raw_y)<abs(raw_x)*0.44f) y=0;
    else if (abs(raw_y)>42 && abs(raw_x)<abs(raw_y)*0.44f) x=0;
    *turn_x=x;
    *turn_y=y;
}
