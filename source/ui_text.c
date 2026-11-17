#include "ui_text.h"
#include "intl.h"
#include "unicode_font_bin.h"
#include <string.h>
static C2D_Font unicode_font;
bool ui_text_init(void){
    unicode_font=C2D_FontLoadFromMem(unicode_font_bin,unicode_font_bin_size);
    return unicode_font!=NULL;
}
void ui_text_exit(void){if(unicode_font)C2D_FontFree(unicode_font);unicode_font=NULL;}
/* Decode a complete UTF-8 scalar, replacing malformed sequences safely. */
static unsigned codepoint(const unsigned char *p,size_t *bytes){
    unsigned c=p[0],n=1,min=0;
    if(c<128){*bytes=1;return c;}
    if(c>=0xc2 && c<=0xdf){n=2;c&=31;min=128;}
    else if(c>=0xe0 && c<=0xef){n=3;c&=15;min=2048;}
    else if(c>=0xf0 && c<=0xf4){n=4;c&=7;min=65536;}
    else {*bytes=1;return 0xfffd;}
    for(unsigned i=1;i<n;i++){
        if(!p[i] || (p[i]&0xc0)!=0x80){*bytes=1;return 0xfffd;}
        c=(c<<6)|(p[i]&63);
    }
    *bytes=n;
    return c<min || c>0x10ffff || (c>=0xd800 && c<=0xdfff)?0xfffd:c;
}
static bool has_glyph(C2D_Font font,unsigned cp){
    FINF_s *info=C2D_FontGetInfo(font);
    return info && C2D_FontGlyphIndexFromCodePoint(font,cp)!=info->alterCharIndex;
}
static C2D_Font select_font(unsigned cp){
    if(unicode_font && has_glyph(unicode_font,cp))return unicode_font;
    return NULL; /* Per-glyph fallback to the console's regional font. */
}
static void finish_line(UiText *text,unsigned first,float width){
    for(unsigned i=first;i<text->count;i++)text->run[i].line_width=width;
    if(width>text->width)text->width=width;
}
void ui_text_parse(UiText *text,C2D_TextBuf buffer,const char *logical){
    memset(text,0,sizeof(*text));
    char visual[INTL_TEXT_CAP];
    static char translated[INTL_TEXT_CAP];
    if(!intl_display(logical?logical:"",translated,sizeof(translated)))return;
    if(!intl_visual(translated,visual,sizeof(visual)))return;
    FINF_s *system=C2D_FontGetInfo(NULL),*custom=unicode_font?C2D_FontGetInfo(unicode_font):system;
    float ascent=system?system->ascent:25;
    if(custom && custom->ascent>ascent)ascent=custom->ascent;
    text->baseline=ascent;
    float line_height=system?system->lineFeed:30;
    if(custom && custom->lineFeed>line_height)line_height=custom->lineFeed;
    char *p=visual;float x=0,y=0;unsigned first=0;
    while(*p && text->count<UI_TEXT_RUNS){
        if(*p=='\n'){finish_line(text,first,x);first=text->count;x=0;y+=line_height;p++;continue;}
        size_t bytes;unsigned cp=codepoint((unsigned char *)p,&bytes);
        C2D_Font font=select_font(cp);
        char *end=p+bytes;
        while(*end && *end!='\n'){
            cp=codepoint((unsigned char *)end,&bytes);
            if(select_font(cp)!=font)break;
            end+=bytes;
        }
        char saved=*end;*end=0;
        UiTextRun *run=&text->run[text->count++];run->x=x;run->y=y;
        C2D_TextFontParse(&run->text,font,buffer,p);
        x+=run->text.width;
        *end=saved;p=end;
    }
    finish_line(text,first,x);
}
void ui_text_optimize(const UiText *text){
    for(unsigned i=0;i<text->count;i++)C2D_TextOptimize(&text->run[i].text);
}
void ui_text_draw(const UiText *text,u32 flags,float x,float y,float z,float sx,float sy,u32 color){
    for(unsigned i=0;i<text->count;i++){
        const UiTextRun *run=&text->run[i];float left=x;
        if((flags&C2D_AlignMask)==C2D_AlignCenter)left-=run->line_width*sx/2;
        else if((flags&C2D_AlignMask)==C2D_AlignRight)left-=run->line_width*sx;
        float baseline=y+run->y*sy+((flags&C2D_AtBaseline)?0:text->baseline*sy);
        C2D_DrawText(&run->text,(flags&~C2D_AlignMask)|C2D_AtBaseline,
            left+run->x*sx,baseline,z,sx,sy,color);
    }
}
