#include "ui_text.h"
#include "intl.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
struct Font {int unused;};static struct Font custom;
static FINF_s custom_info={999,37,34},system_info={999,30,25};
static unsigned draw_count;static float last_x,last_y;
C2D_Font C2D_FontLoadFromMem(const void *p,size_t n){assert(p&&n);return &custom;}
void C2D_FontFree(C2D_Font f){assert(f==&custom);}
FINF_s *C2D_FontGetInfo(C2D_Font f){return f?&custom_info:&system_info;}
int C2D_FontGlyphIndexFromCodePoint(C2D_Font f,u32 cp){
 if(f)return cp<128 || (cp>=0x590 && cp<=0x6ff) || (cp>=0xfb00 && cp<=0xfeff)?1:999;
 return cp<128 || (cp>=0x3000 && cp<=0x9fff)?2:999;
}
void C2D_TextFontParse(C2D_Text *t,C2D_Font f,C2D_TextBuf b,const char *s){
 (void)b;t->font=f;snprintf(t->value,sizeof(t->value),"%s",s);t->width=(float)strlen(s)*10;
}
void C2D_TextOptimize(const C2D_Text *t){assert(t);}
void C2D_DrawText(const C2D_Text *t,u32 flags,float x,float y,float z,float sx,float sy,u32 color){
 (void)t;(void)z;(void)sx;(void)sy;(void)color;
 assert(flags&C2D_AtBaseline);last_x=x;last_y=y;draw_count++;
}
int main(void){
 assert(ui_text_init());static UiText text;
 ui_text_parse(&text,NULL,"سلام 東京 ABC");
 assert(text.count>=2);
 bool arabic=false,cjk=false;
 for(unsigned i=0;i<text.count;i++){
  if(strstr(text.run[i].text.value,"東京")){assert(!text.run[i].text.font);cjk=true;}
  if(strstr(text.run[i].text.value,"ﻡﻼﺳ")){assert(text.run[i].text.font==&custom);arabic=true;}
 }
 assert(arabic&&cjk);
 ui_text_parse(&text,NULL,"ABC\n東京");assert(text.count==2&&text.run[1].y==37);
 ui_text_draw(&text,C2D_AlignCenter|C2D_WithColor,100,0,0,1,1,0);
 assert(draw_count==2 && fabsf(last_x-(100-text.run[1].line_width/2))<.01f && last_y==71);
 ui_text_parse(&text,NULL,"ABC");
 ui_text_draw(&text,C2D_AlignRight,100,0,0,1,1,0);assert(last_x==70);
 ui_text_exit();ui_text_parse(&text,NULL,"東京");assert(text.count==1&&!text.run[0].text.font);
 puts("PASS mixed Arabic/CJK/Latin font runs, shared baselines, multiline centering, right alignment and font-load fallback");
}
