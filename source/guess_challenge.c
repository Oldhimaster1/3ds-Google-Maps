#include "guess_challenge.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
static uint32_t next(uint32_t *s){*s=*s*1664525u+1013904223u;return *s;}
static uint32_t check(uint32_t seed,unsigned difficulty){
 uint32_t h=2166136261u;
 uint32_t values[3]={guess_dataset_id,seed,difficulty};
 for(unsigned i=0;i<3;i++)for(unsigned b=0;b<4;b++)h=(h^((values[i]>>(b*8))&255u))*16777619u;
 return h&65535u;
}
bool guess_challenge_make(GuessChallenge *c,uint32_t seed,unsigned difficulty){
 if(!c||difficulty>2||guess_location_count<GUESS_ROUNDS||guess_location_count>256)return false;
 unsigned order[256],count=0;
 bool expedition=(seed&0xFF000000u)==GUESS_EXPEDITION_SEED;
 for(unsigned i=0;i<guess_location_count;i++)if(!expedition||guess_locations[i].landmark)order[count++]=i;
 if(count<GUESS_ROUNDS)return false;
 uint32_t state=seed^guess_dataset_id;
 for(unsigned i=count-1;i>0;i--){unsigned j=next(&state)%(i+1);unsigned t=order[i];order[i]=order[j];order[j]=t;}
 c->seed=seed;c->difficulty=difficulty;
 /* Prefer a new country each round, while retaining exact seeded replays. */
 for(unsigned round=0;round<GUESS_ROUNDS;round++){
  unsigned pick=round;
  for(unsigned j=round;j<count;j++){
   bool used=false;for(unsigned k=0;k<round;k++)
    if(!strcmp(guess_locations[order[k]].country,guess_locations[order[j]].country))used=true;
   if(!used){pick=j;break;}
  }
  unsigned swap=order[round];order[round]=order[pick];order[pick]=swap;
 }
 memcpy(c->location,order,sizeof(c->location));return true;
}
bool guess_challenge_code(const GuessChallenge *c,char *out,size_t cap){
 if(!c||!out||c->difficulty>2)return false;
 int n=snprintf(out,cap,"SG1-%08lX-%08lX-%u-%04lX",(unsigned long)guess_dataset_id,
                (unsigned long)c->seed,c->difficulty,(unsigned long)check(c->seed,c->difficulty));
 return n>0&&(size_t)n<cap;
}
static bool parse_canonical(GuessChallenge *c,const char *text){
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

/* Accept photographed, pasted and lowercase codes, including omitted separators. */
bool guess_challenge_parse(GuessChallenge *c,const char *text){
 if(!c||!text)return false;
 char compact[25],code[29];unsigned n=0;
 for(;*text;text++){
  unsigned char ch=(unsigned char)*text;
  if(ch=='-'||isspace(ch))continue;
  if(!isalnum(ch)||n>=24)return false;
  compact[n++]=(char)toupper(ch);
 }
 if(n!=24)return false;
 compact[n]=0;
 snprintf(code,sizeof(code),"%.3s-%.8s-%.8s-%.1s-%.4s",compact,compact+3,compact+11,compact+19,compact+20);
 return parse_canonical(c,code);
}
bool guess_country_parse(const char *answer,char out[3]){
 if(!answer||!out)return false;
 char text[64];size_t n=0;
 while(isspace((unsigned char)*answer))answer++;
 while(*answer&&n+1<sizeof(text))text[n++]=(char)tolower((unsigned char)*answer++);
 if(*answer)return false;
 while(n&&isspace((unsigned char)text[n-1]))n--;
 text[n]=0;
 if(n==2&&isalpha((unsigned char)text[0])&&isalpha((unsigned char)text[1])){
  out[0]=(char)toupper((unsigned char)text[0]);out[1]=(char)toupper((unsigned char)text[1]);out[2]=0;if(!strcmp(out,"UK"))memcpy(out,"GB",3);return true;
 }
 static const char *names[][2]={
 {"france","FR"},{"united states","US"},{"usa","US"},{"united kingdom","GB"},{"uk","GB"},{"britain","GB"},
 {"germany","DE"},{"austria","AT"},{"netherlands","NL"},{"spain","ES"},{"italy","IT"},{"finland","FI"},
 {"czechia","CZ"},{"czech republic","CZ"},{"japan","JP"},{"singapore","SG"},{"taiwan","TW"},{"south korea","KR"},
 {"australia","AU"},{"south africa","ZA"},{"kenya","KE"},{"brazil","BR"},{"argentina","AR"},{"mexico","MX"},
 {"canada","CA"},{"portugal","PT"},{"denmark","DK"},{"norway","NO"},{"sweden","SE"},{"iceland","IS"},
 {"poland","PL"},{"greece","GR"},{"turkey","TR"},{"romania","RO"},{"belgium","BE"},{"switzerland","CH"},
 {"estonia","EE"},{"latvia","LV"},{"lithuania","LT"},{"ireland","IE"},{"new zealand","NZ"},
 {"thailand","TH"},{"indonesia","ID"},{"india","IN"},{"egypt","EG"},{"morocco","MA"},
 {"china","CN"},{"russia","RU"},{"ukraine","UA"},{"hungary","HU"},{"croatia","HR"},{"slovenia","SI"}};
 for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)if(!strcmp(text,names[i][0])){memcpy(out,names[i][1],3);return true;}
 return false;
}
