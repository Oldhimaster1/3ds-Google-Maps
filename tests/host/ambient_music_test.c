#include "ambient_music.h"
#include <3ds.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static unsigned starts,stops,queued,pauses,resumes,cleared;
static bool fail,fail_alloc;
static float gain;
int ndspInit(void){starts++;return fail?-1:0;}
void ndspExit(void){stops++;}
void *linearAlloc(size_t n){return fail_alloc?NULL:malloc(n);}
void linearFree(void *p){free(p);}
void DSP_FlushDataCache(const void *p,size_t n){assert(p && n==256000);}
void ndspChnReset(unsigned c){assert(c==23);}
void ndspChnSetInterp(unsigned c,int i){assert(c==23 && i==NDSP_INTERP_LINEAR);}
void ndspChnSetRate(unsigned c,float r){assert(c==23 && r==16000);}
void ndspChnSetFormat(unsigned c,int f){assert(c==23 && f==NDSP_FORMAT_MONO_PCM16);}
void ndspChnSetMix(unsigned c,const float *mix){assert(c==23 && mix[0]>=0 && mix[0]<=1 && mix[0]==mix[1]);gain=mix[0];}
void ndspChnWaveBufAdd(unsigned c,ndspWaveBuf *w){
    assert(c==23 && w->looping && w->nsamples==128000);
    const s16 *pcm=w->data_vaddr;long amplitude=0;
    for(unsigned i=0;i<w->nsamples;i++){assert(pcm[i]>=-2100 && pcm[i]<=2100);amplitude+=abs(pcm[i]);}
    assert(amplitude>1000000 && pcm[0]==0 && abs(pcm[w->nsamples-1])<2);queued++;
}
void ndspChnSetPaused(unsigned c,bool p){assert(c==23);if(p)pauses++;else resumes++;}
void ndspChnWaveBufClear(unsigned c){assert(c==23);cleared++;}
int main(void){
    ambient_music_update(false);assert(starts==0);
    ambient_music_update(true);ambient_music_update(true);assert(starts==1 && queued==1);
    ambient_music_set_volume(0);assert(gain==0);
    ambient_music_set_volume(100);assert(gain==1);
    ambient_music_set_volume(1000);assert(gain==1);
    ambient_music_set_volume(25);assert(gain==.25f);
    ambient_music_update(false);ambient_music_update(false);assert(pauses==1);
    ambient_music_update(true);assert(resumes==1 && queued==1);
    ambient_music_exit();assert(cleared==1 && stops==1);
    ambient_music_set_volume(0);ambient_music_update(true);assert(starts==1);
    ambient_music_set_volume(55);
    fail=true;ambient_music_update(true);ambient_music_update(true);assert(starts==2 && queued==1);
    assert(ambient_music_status()==AMBIENT_DSP_ERROR);
    fail=false;ambient_music_retry();assert(ambient_music_status()==AMBIENT_READY);
    ambient_music_update(true);assert(starts==3 && queued==2);
    ambient_music_exit();fail_alloc=true;ambient_music_update(true);
    assert(starts==4 && stops==3 && queued==2);
    assert(ambient_music_status()==AMBIENT_MEMORY_ERROR);ambient_music_exit();
    puts("PASS original audio loop samples, pause/resume, DSP failure, allocation failure and cleanup");
}
