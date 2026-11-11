#include "guess_challenge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 assert(guess_location_count>=5);
 for(uint32_t seed=0;seed<10000;seed++)for(unsigned d=0;d<3;d++){
  GuessChallenge a,b;char code[40];assert(guess_challenge_make(&a,seed,d));
  assert(guess_challenge_code(&a,code,sizeof(code)));assert(guess_challenge_parse(&b,code));
  assert(a.seed==b.seed&&a.difficulty==b.difficulty&&!memcmp(a.location,b.location,sizeof(a.location)));
  for(unsigned i=0;i<5;i++){assert(a.location[i]<guess_location_count);for(unsigned j=0;j<i;j++)assert(a.location[i]!=a.location[j]);}
  code[10]=code[10]=='A'?'B':'A';assert(!guess_challenge_parse(&b,code));
 }
 assert(guess_challenge_day(86399)==0&&guess_challenge_day(86400)==1&&guess_challenge_day(-1)==0);
 assert(!guess_challenge_parse(&(GuessChallenge){0},"SG1-00000000-00000000-0-0000"));
 puts("Challenge codes: 30k deterministic roundtrips, unique rounds, dataset mismatch and corruption passed");
}
