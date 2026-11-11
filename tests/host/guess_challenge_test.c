#include "guess_challenge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 assert(guess_location_count>=28);
 for(uint32_t seed=0;seed<10000;seed++)for(unsigned d=0;d<3;d++){
  GuessChallenge a,b;char code[40];assert(guess_challenge_make(&a,seed,d));
  assert(guess_challenge_code(&a,code,sizeof(code)));assert(guess_challenge_parse(&b,code));
  assert(a.seed==b.seed&&a.difficulty==b.difficulty&&!memcmp(a.location,b.location,sizeof(a.location)));
  for(unsigned i=0;i<5;i++){assert(a.location[i]<guess_location_count);for(unsigned j=0;j<i;j++)assert(a.location[i]!=a.location[j]);}
  char spaced[80];snprintf(spaced,sizeof(spaced),"  %s \n",code);
  for(char *p=spaced;*p;p++)if(*p>='A'&&*p<='Z')*p+=32;
  assert(guess_challenge_parse(&b,spaced));
  for(unsigned i=0;i<5;i++)for(unsigned j=0;j<i;j++)
   assert(strcmp(guess_locations[a.location[i]].country,guess_locations[a.location[j]].country));
  code[10]=code[10]=='A'?'B':'A';assert(!guess_challenge_parse(&b,code));
 }
 assert(guess_challenge_day(86399)==0&&guess_challenge_day(86400)==1&&guess_challenge_day(-1)==0);
 assert(!guess_challenge_parse(&(GuessChallenge){0},"SG1-00000000-00000000-0-0000"));
 GuessChallenge expedition;
 assert(guess_challenge_make(&expedition,GUESS_EXPEDITION_SEED,0));
 for(unsigned i=0;i<GUESS_ROUNDS;i++)assert(guess_locations[expedition.location[i]].landmark);
 char country[3];assert(guess_country_parse("  Japan  ",country)&&!strcmp(country,"JP"));
 assert(guess_country_parse("ca",country)&&!strcmp(country,"CA"));
 assert(guess_country_parse("United States",country)&&!strcmp(country,"US"));
 assert(!guess_country_parse("",country));assert(!guess_country_parse("a really long invalid answer",country));
 puts("Challenge codes: 30k deterministic roundtrips, unique rounds, dataset mismatch and corruption passed");
}
