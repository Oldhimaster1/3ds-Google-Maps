#ifndef STREET_FLY_LOGIC_H
#define STREET_FLY_LOGIC_H
#include <stdint.h>
#define STREET_FLY_MAX_MS 1600
#define STREET_FLY_DEFAULT_MS 700
int street_fly_clamp_ms(int value);
float street_fly_progress(uint64_t now, uint64_t start, int duration_ms);
float street_fly_scale(float progress);
#endif
