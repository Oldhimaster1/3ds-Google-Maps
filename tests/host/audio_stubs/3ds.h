#ifndef TEST_AUDIO_3DS_H
#define TEST_AUDIO_3DS_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int16_t s16;
typedef struct {const void *data_vaddr;unsigned nsamples;bool looping;} ndspWaveBuf;
#define R_FAILED(x) ((x)<0)
#define NDSP_INTERP_LINEAR 1
#define NDSP_FORMAT_MONO_PCM16 2
int ndspInit(void);
void ndspExit(void);
void *linearAlloc(size_t);
void linearFree(void *);
void DSP_FlushDataCache(const void *,size_t);
void ndspChnReset(unsigned);
void ndspChnSetInterp(unsigned,int);
void ndspChnSetRate(unsigned,float);
void ndspChnSetFormat(unsigned,int);
void ndspChnSetMix(unsigned,const float *);
void ndspChnWaveBufAdd(unsigned,ndspWaveBuf *);
void ndspChnSetPaused(unsigned,bool);
void ndspChnWaveBufClear(unsigned);
#endif
