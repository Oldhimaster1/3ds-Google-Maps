#include "guess_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static int almost(double x,double y,double e){return fabs(x-y)<e;}
int main(void){
    GuessGame g;guess_init(&g,0xabc123);assert(g.phase==GUESS_MENU);
    guess_begin(&g,GUESS_WORLD,GUESS_MOVING,120);assert(g.phase==GUESS_FINDING);
    GuessPoint c;int diversity[4]={0};
    for(int i=0;i<24;i++){
       assert(guess_random_candidate(&g,&c));
       assert(isfinite(c.lat)&&c.lat>=-85&&c.lat<=85);
       assert(isfinite(c.lon)&&c.lon>=-180&&c.lon<=180);
       diversity[c.lon<0?(c.lat<0?0:1):(c.lat<0?2:3)]++;
    }
    assert(!guess_random_candidate(&g,&c));
    assert(almost(guess_distance_km((GuessPoint){0,0},(GuessPoint){0,0}),0,1e-9));
    assert(almost(guess_distance_km((GuessPoint){0,0},(GuessPoint){0,1}),111.195,0.1));
    assert(almost(guess_distance_km((GuessPoint){0,179},(GuessPoint){0,-179}),222.39,1));
    assert(guess_score_km(0,GUESS_WORLD)==5000);
    assert(guess_score_km(0.1,GUESS_WORLD)==5000);
    assert(guess_score_km(1492.7,GUESS_WORLD)>1830);
    assert(guess_score_km(1492.7,GUESS_WORLD)<1850);
    assert(guess_score_km(90000,GUESS_WORLD)==0);
    assert(guess_score_km(NAN,GUESS_WORLD)==0);
    guess_begin(&g,GUESS_WORLD,GUESS_MOVING,2);
    assert(guess_accept_start(&g,(GuessPoint){48,2},1000));
    assert(!guess_candidate_acceptable(&g,(GuessPoint){48.1,2.1}));
    guess_set_pin(&g,(GuessPoint){48.0001,2.0001});
    guess_tick(&g,1500);assert(g.remaining_ms==1500);
    guess_toggle_pause(&g,1500);assert(g.phase==GUESS_PAUSED);
    guess_tick(&g,1650);assert(g.remaining_ms==1500);
    guess_toggle_pause(&g,3000);assert(g.phase==GUESS_PLAYING);
    assert(guess_submit(&g));assert(g.phase==GUESS_RESULT);assert(g.total==5000);
    assert(guess_advance(&g)&&g.phase==GUESS_FINDING&&g.round==1);
    for(int k=1;k<5;k++){
        assert(guess_accept_start(&g,(GuessPoint){48.+k,2.+k},4000+k*100));
        guess_set_pin(&g,(GuessPoint){48.+k,2.+k});
        assert(guess_submit(&g));assert(guess_advance(&g));
    }
    assert(g.phase==GUESS_COMPLETE&&g.total==25000);
    GuessPoint center={0,0};
    GuessPoint p=guess_map_at_pixel(center,2,210,120,320,240);
    float x=0,y=0;guess_map_project(center,p,2,320,240,&x,&y);
    assert(almost(x,210,0.1)&&almost(y,120,0.1));
    center=(GuessPoint){0,179};guess_map_center_delta(&center,3,25,0);assert(center.lon<-175);
    guess_begin(&g,GUESS_USA,GUESS_NMPZ,30);
    for(int i=0;i<24;i++){assert(guess_random_candidate(&g,&c));assert(c.lat>=24&&c.lat<=49&&c.lon>=-125&&c.lon<=-66);}
    guess_begin(&g,GUESS_EUROPE,GUESS_NO_MOVE,0);
    for(int i=0;i<24;i++){assert(guess_random_candidate(&g,&c));assert(c.lat>=36&&c.lat<=70&&c.lon>=-11&&c.lon<=51);}
    puts("StreetGuess core host tests passed");
    return 0;
}