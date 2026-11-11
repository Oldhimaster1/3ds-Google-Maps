#ifndef STUB_EXPLORE_H
#define STUB_EXPLORE_H
#include "citro3d.h"
#include <stdbool.h>
#define EXPLORE_STREET 3
bool explore_load(int,unsigned);void explore_unload(void);void explore_draw(C3D_RenderTarget*,float,float,float,float,float);

#endif