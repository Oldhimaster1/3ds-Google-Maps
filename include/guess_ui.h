#ifndef STREETGUESS_UI_H
#define STREETGUESS_UI_H
#include <stdbool.h>
#include <citro3d.h>
/* The game owns its entire UI and map camera; normal Maps state is untouched. */
bool guess_ui_enter(void);
bool guess_ui_active(void);
void guess_ui_update(void);
void guess_ui_draw(C3D_RenderTarget *top,C3D_RenderTarget *bottom);
void guess_ui_leave(void);
#endif