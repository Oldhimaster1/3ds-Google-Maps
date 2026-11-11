#ifndef STUB_3DS_H
#define STUB_3DS_H
#include "citro3d.h"
#define KEY_B 2
#define KEY_A 1
#define KEY_START 8
#define KEY_SELECT 4
#define KEY_TOUCH (1u<<20)
#define KEY_X (1u<<10)
#define KEY_Y (1u<<11)
#define KEY_L (1u<<9)
#define KEY_R (1u<<8)
#define KEY_DLEFT (1u<<5)
#define KEY_DRIGHT (1u<<4)
#define KEY_DUP (1u<<6)
#define KEY_DDOWN (1u<<7)
#define R_FAILED(x) ((x)<0)
#define R_SUCCEEDED(x) ((x)>=0)
typedef struct {int16_t dx,dy;} circlePosition;
typedef struct {uint16_t px,py;} touchPosition;
typedef struct {int16_t x,y,z;} angularRate;
typedef struct {int16_t x,y,z;} accelVector;
u64 svcGetSystemTick(void);u64 osGetTime(void);
void gfxSet3D(bool);
void hidScanInput(void);u32 hidKeysDown(void);u32 hidKeysHeld(void);
void hidTouchRead(touchPosition*);void hidCircleRead(circlePosition*);void hidGyroRead(angularRate*);void hidAccelRead(accelVector*);
int HIDUSER_DisableGyroscope(void);int HIDUSER_EnableGyroscope(void);int HIDUSER_GetGyroscopeRawToDpsCoefficient(float*);
int HIDUSER_EnableAccelerometer(void);int HIDUSER_DisableAccelerometer(void);
typedef struct {int unused;} SwkbdState;
#define SWKBD_TYPE_NORMAL 0
#define SWKBD_BUTTON_CONFIRM 1
void swkbdInit(SwkbdState *,int,int,int);
void swkbdSetHintText(SwkbdState *,const char *);
int swkbdInputText(SwkbdState *,char *,size_t);
#endif
