#ifndef MAPS_UI_TEXT_H
#define MAPS_UI_TEXT_H
#include <citro2d.h>
#define UI_TEXT_RUNS 128
typedef struct {C2D_Text text;float x,y,line_width;} UiTextRun;
typedef struct {UiTextRun run[UI_TEXT_RUNS];unsigned count;float width,baseline;} UiText;
bool ui_text_init(void);
void ui_text_exit(void);
void ui_text_parse(UiText *text,C2D_TextBuf buffer,const char *logical);
void ui_text_optimize(const UiText *text);
void ui_text_draw(const UiText *text,u32 flags,float x,float y,float z,float sx,float sy,u32 color);
#endif
