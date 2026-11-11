#include "guess_challenge.h"
#include <stdio.h>
#include <string.h>
static uint32_t next(uint32_t *s){*s=*s*1664525u+1013904223u;return *s;}
static uint32_t check(uint32_t seed,unsigned difficulty){
 uint32_t h=2166136261u;
 uint32_t values[3]={guess_dataset_id,seed,difficulty};
 for(unsigned i=0;i<3;i++)for(unsigned b=0;b<4;b++)h=(h^((values[i]>>(b*8))&255u))*16777619u;
 return h&65535u;
}
bool guess_challenge_make(GuessChallenge *c,uint32_t seed,unsigned difficulty){
 if(!c||difficulty>2||guess_location_count<GUESS_ROUNDS||guess_location_count>256)return false;
 unsigned order[256];for(unsigned i=0;i<guess_location_count;i++)order[i]=i;
 uint32_t state=seed^guess_dataset_id;
 for(unsigned i=guess_location_count-1;i>0;i--){unsigned j=next(&state)%(i+1);unsigned t=order[i];order[i]=order[j];order[j]=t;}
 c->seed=seed;c->difficulty=difficulty;
 memcpy(c->location,order,sizeof(c->location));return true;
}
bool guess_challenge_code(const GuessChallenge *c,char *out,size_t cap){
 if(!c||!out||c->difficulty>2)return false;
 int n=snprintf(out,cap,"SG1-%08lX-%08lX-%u-%04lX",(unsigned long)guess_dataset_id,
                (unsigned long)c->seed,c->difficulty,(unsigned long)check(c->seed,c->difficulty));
 return n>0&&(size_t)n<cap;
}
bool guess_challenge_parse(GuessChallenge *c,const char *text){
 if(!c||!text||strlen(text)!=28)return false;
 unsigned long data,seed,sum;unsigned difficulty;int used=0;
 if(sscanf(text,"SG1-%8lx-%8lx-%1u-%4lx%n",&data,&seed,&difficulty,&sum,&used)!=4||used!=28||
    data!=guess_dataset_id||seed>UINT32_MAX||difficulty>2||sum!=check((uint32_t)seed,difficulty))return false;
 GuessChallenge parsed;if(!guess_challenge_make(&parsed,(uint32_t)seed,difficulty))return false;
 char canonical[40];guess_challenge_code(&parsed,canonical,sizeof(canonical));
 if(strcmp(canonical,text))return false;
 *c=parsed;return true;
}
uint32_t guess_challenge_day(int64_t seconds){return seconds<0?0:(uint32_t)(seconds/86400);}
