#ifndef STREET_LOOK_INPUT_H
#define STREET_LOOK_INPUT_H
/* The original 3DS Circle Pad can report small vertical noise during
 * horizontal movement. Axis-lock prevents that from tilting the horizon. */
void street_look_axes(int raw_x, int raw_y, float *turn_x, float *turn_y);
#endif
