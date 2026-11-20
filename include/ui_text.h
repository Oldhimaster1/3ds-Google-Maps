#ifndef MAPS_UI_TEXT_H
#define MAPS_UI_TEXT_H
#include <citro2d.h>
bool ui_text_init(void);
void ui_text_exit(void);
void ui_text_parse(C2D_Text *text,C2D_TextBuf buffer,const char *logical);
#endif
