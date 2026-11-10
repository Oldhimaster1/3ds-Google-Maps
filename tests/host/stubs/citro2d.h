#ifndef STUB_CITRO2D_H
#define STUB_CITRO2D_H
#include <stddef.h>
#include "citro3d.h"
typedef struct C2D_TextBuf_s* C2D_TextBuf;
typedef struct {char _dummy;} C2D_Text;
#define C2D_WithColor 1
C2D_TextBuf C2D_TextBufNew(size_t);
void C2D_TextBufDelete(C2D_TextBuf);
void C2D_TextBufClear(C2D_TextBuf);
void C2D_TextParse(C2D_Text*, C2D_TextBuf,const char*);
void C2D_TextOptimize(C2D_Text*);
bool C2D_DrawText(const C2D_Text*,u32,float,float,float,float,float,u32);
void C2D_Prepare(void);
void C2D_TargetClear(C3D_RenderTarget*,u32);
void C2D_SceneBegin(C3D_RenderTarget*);
u32 C2D_Color32(int,int,int,int);
void C2D_DrawRectSolid(float,float,float,float,float,u32);
void C2D_DrawCircleSolid(float,float,float,float,u32);
void C2D_DrawLine(float,float,u32,float,float,u32,float,float);
#endif