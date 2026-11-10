#include "guess_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
/* Deterministic property testing across 48,000 random geographic candidates. */
static void check(int cond,int n){if(!cond){fprintf(stderr,"Stress check failed %d\n",n);exit(1);}}
int main(void){
    unsigned quadrants[4]={0};
    /* Geographic candidate diversity is NOT the same as actual Google
     * panorama coverage. Count occupied 1-degree cells as one diagnostic. */
    unsigned char *cells=calloc(180u*360u,1u);
    check(cells!=NULL,0);
    uint64_t samples=0;
    for(uint64_t seed=1;seed<=2000;seed++){
        /* Do NOT seed with multiples of the generator's counter increment:
         * doing so makes the 2,000 test streams overlap heavily and makes
         * geographic-diversity metrics meaningless. Hash each seed first. */
        uint64_t z=seed;
        z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
        z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);
        z^=z>>31;
        GuessGame g;guess_init(&g,z);
        guess_begin(&g,GUESS_WORLD,GUESS_MOVING,120);
        GuessPoint old={INFINITY,INFINITY};
        for(int k=0;k<GUESS_MAX_CANDIDATE_TRIES;k++){
            GuessPoint p;
            check(guess_random_candidate(&g,&p),1);
            check(isfinite(p.lat)&&isfinite(p.lon),2);
            check(p.lat>=-85.051129&&p.lat<=85.051129&&p.lon>=-180&&p.lon<=180,3);
            check(!(old.lat==p.lat&&old.lon==p.lon),4);
            quadrants[(p.lat>=0)*2+(p.lon>=0)]++;
            unsigned y=(unsigned)floor(p.lat+90.0),x=(unsigned)floor(p.lon+180.0);
            if(y>179)y=179;
            if(x>359)x=359;
            cells[y*360u+x]=1;
            old=p;samples++;
        }
        check(!guess_random_candidate(&g,&old),5);
    }
    for(int q=0;q<4;q++)check(quadrants[q]>300,6);
    unsigned occupied=0;
    for(unsigned k=0;k<180u*360u;k++)occupied+=cells[k]!=0;
    free(cells);
    check(occupied>2000,8);
    /* Scoring decreases monotonically for all modes, with no overflow. */
    for(int mode=0;mode<GUESS_MAP_COUNT;mode++){
        unsigned previous=5000;
        for(int d=0;d<=20000;d+=5){
            unsigned score=guess_score_km(d,(GuessRegion)mode);
            check(score<=previous&&score<=5000,7);
            previous=score;
        }
    }
    printf("StreetGuess sampled %llu candidate coordinates across %u geographic 1-degree cells; geographic diversity and scoring invariants pass (not Google coverage)\n",
           (unsigned long long)samples,occupied);
    return 0;
}