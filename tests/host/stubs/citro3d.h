#ifndef STUB_CITRO3D_H
#define STUB_CITRO3D_H
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t u32; typedef uint64_t u64; typedef uint16_t u16;
typedef struct C3D_RenderTarget C3D_RenderTarget;
#define C3D_FRAME_SYNCDRAW 0
bool C3D_FrameBegin(int);void C3D_FrameEnd(int);
float C3D_AngleFromDegrees(float);
#endif