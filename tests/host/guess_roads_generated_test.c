#include "guess_roads.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(int argc,char **argv){
    assert(argc==2);
    GuessRoads roads={0};
    assert(guess_roads_open(&roads,argv[1]));
    assert(roads.records>=2&&roads.records<=10);
    uint64_t rng=0x0123456789012345ULL;
    for(int i=0;i<300;i++){
        GuessPoint p;
        assert(guess_roads_sample(&roads,&rng,GUESS_USA,&p));
        assert(p.lat>=40.0&&p.lat<=40.01);
        assert(p.lon>=-74&&p.lon<=-73.99);
    }
    /* The builder did not tag any roads as European. */
    GuessPoint q={0};
    assert(!guess_roads_sample(&roads,&rng,GUESS_EUROPE,&q));
    guess_roads_close(&roads);
    puts("StreetGuess GeoJSON generator -> 3DS reader roundtrip tests passed");
    return 0;
}