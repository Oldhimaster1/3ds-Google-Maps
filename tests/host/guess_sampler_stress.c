#include "guess_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
/* Deterministic property testing across 48,000 random geographic candidates. */
static void check(int cond,int n){if(!cond){fprintf(stderr,"Stress check failed %d\n",n);exit(1);}}
int main(void){
    unsigned quadrants[4]={0};
    uint64_t samples=0;
    for(uint64_t seed=1;seed<=2000;seed++){
        GuessGame g;guess_init(&g,seed*0x9e3779b97f4a7c15ULL);
        guess_begin(&g,GUESS_WORLD,GUESS_MOVING,120);
        GuessPoint old={INFINITY,INFINITY};
        for(int k=0;k<GUESS_MAX_CANDIDATE_TRIES;k++){
            GuessPoint p;
            check(guess_random_candidate(&g,&p),1);
            check(isfinite(p.lat)&&isfinite(p.lon),2);
            check(p.lat>=-85.051129&&p.lat<=85.051129&&p.lon>=-180&&p.lon<=180,3);
            check(!(old.lat==p.lat&&old.lon==p.lon),4);
            quadrants[(p.lat>=0)*2+(p.lon>=0)]++;
            old=p;samples++;
        }
        check(!guess_random_candidate(&g,&old),5);
    }
    for(int q=0;q<4;q++)check(quadrants[q]>300,6);
    /* Scoring decreases monotonically for all modes, with no overflow. */
    for(int mode=0;mode<GUESS_MAP_COUNT;mode++){
        unsigned previous=5000;
        for(int d=0;d<=20000;d+=5){
            unsigned score=guess_score_km(d,(GuessRegion)mode);
            check(score<=previous&&score<=5000,7);
            previous=score;
        }
    }
    printf("StreetGuess sampled %llu distinct consecutive candidates; distribution and scoring invariants pass\n",
           (unsigned long long)samples);
    return 0;
}