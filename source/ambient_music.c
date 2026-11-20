/* Original ambient composition for Maps. No sampled or third-party audio.
 * A quiet four-chord, eight-second loop; channel 23 is reserved here. */
#include "ambient_music.h"
#include <3ds.h>
#include <math.h>
#include <string.h>
#define RATE 16000
#define SAMPLES (RATE*8)
#define CHANNEL 23
static s16 *pcm;
static ndspWaveBuf wave;
static bool initialized,playing,unavailable;
static void synthesize(void){
    static const float chords[4][3]={{130.8128f,164.8138f,195.9977f},
        {110.0f,130.8128f,164.8138f},{87.3071f,110.0f,130.8128f},
        {97.9989f,123.4708f,146.8324f}};
    const float tau=6.28318530718f;
    for(unsigned i=0;i<SAMPLES;i++){
        unsigned chord=i/(RATE*2),local=i%(RATE*2);
        float t=local/(float)RATE;
        /* Zero amplitude and zero slope at chord boundaries: no clicks. */
        float envelope=sinf(3.14159265359f*t/2.0f);envelope*=envelope;
        float value=0;
        for(unsigned note=0;note<3;note++)
            value+=sinf(tau*chords[chord][note]*t)*700.0f;
        pcm[i]=(s16)(value*envelope);
    }
}
void ambient_music_update(bool active){
    if(unavailable)return;
    if(active && !initialized){
        /* Homebrew without DSP firmware stays usable with silent audio. */
        if(R_FAILED(ndspInit())){unavailable=true;return;}
        pcm=linearAlloc(SAMPLES*sizeof(*pcm));
        if(!pcm){ndspExit();unavailable=true;return;}
        synthesize();DSP_FlushDataCache(pcm,SAMPLES*sizeof(*pcm));
        ndspChnReset(CHANNEL);ndspChnSetInterp(CHANNEL,NDSP_INTERP_LINEAR);
        ndspChnSetRate(CHANNEL,RATE);ndspChnSetFormat(CHANNEL,NDSP_FORMAT_MONO_PCM16);
        float mix[12]={0};mix[0]=mix[1]=0.55f;ndspChnSetMix(CHANNEL,mix);
        memset(&wave,0,sizeof(wave));wave.data_vaddr=pcm;
        wave.nsamples=SAMPLES;wave.looping=true;
        ndspChnWaveBufAdd(CHANNEL,&wave);initialized=true;playing=true;
    }
    if(initialized && playing!=active){ndspChnSetPaused(CHANNEL,!active);playing=active;}
}
void ambient_music_exit(void){
    if(initialized){ndspChnWaveBufClear(CHANNEL);ndspChnReset(CHANNEL);
        ndspExit();linearFree(pcm);}
    pcm=NULL;initialized=playing=false;unavailable=false;
}
