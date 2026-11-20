#ifndef TEXT_TEST_C2D_H
#define TEXT_TEST_C2D_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint32_t u32;
typedef struct Font *C2D_Font;
typedef struct {uint16_t alterCharIndex;unsigned char lineFeed,ascent;} FINF_s;
typedef struct Buffer *C2D_TextBuf;
typedef struct {float width;C2D_Font font;char value[1024];} C2D_Text;
#define C2D_AtBaseline 1
#define C2D_WithColor 2
#define C2D_AlignLeft 0
#define C2D_AlignRight 4
#define C2D_AlignCenter 8
#define C2D_AlignMask 12
C2D_Font C2D_FontLoadFromMem(const void *,size_t);
void C2D_FontFree(C2D_Font);
FINF_s *C2D_FontGetInfo(C2D_Font);
int C2D_FontGlyphIndexFromCodePoint(C2D_Font,u32);
void C2D_TextFontParse(C2D_Text *,C2D_Font,C2D_TextBuf,const char *);
void C2D_TextOptimize(const C2D_Text *);
void C2D_DrawText(const C2D_Text *,u32,float,float,float,float,float,u32);
#endif
