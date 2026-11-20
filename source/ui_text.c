#include "ui_text.h"
#include "intl.h"
#include "unicode_font_bin.h"
static C2D_Font unicode_font;
bool ui_text_init(void){
    unicode_font=C2D_FontLoadFromMem(unicode_font_bin,unicode_font_bin_size);
    return unicode_font!=NULL;
}
void ui_text_exit(void){if(unicode_font)C2D_FontFree(unicode_font);unicode_font=NULL;}
void ui_text_parse(C2D_Text *text,C2D_TextBuf buffer,const char *logical){
    char visual[INTL_TEXT_CAP];
    const char *translated=intl_translate(logical);
    if(!translated)translated="";
    if(!intl_visual(translated,visual,sizeof(visual)))visual[0]=0;
    /* Preserve Japanese/CJK support from the console's regional font. */
    C2D_Font font=unicode_font;
    const unsigned char *p=(const unsigned char *)translated;
    while(*p){if(*p>=0xe3 && *p<=0xed){font=NULL;break;}p++;}
    C2D_TextFontParse(text,font,buffer,visual);
}
