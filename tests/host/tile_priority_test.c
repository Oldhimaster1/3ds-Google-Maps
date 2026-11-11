#include "tile_priority.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    TileViewport v={.zoom=11,.world=2048,.start_x=123,.start_y=900,
                    .tiles_x=3,.tiles_y=2,.center_x=124,.center_y=900,
                    .gps_follow=1,.valid=1};
    assert(tile_view_score(&v,124,900,11,0)==0);
    assert(tile_view_score(&v,123,900,11,0)==1);
    assert(tile_view_score(&v,125,901,11,0)>0);
    assert(tile_view_score(&v,123,900,10,0)==-1);
    assert(tile_view_score(&v,126,900,11,1)==-1);
    assert(tile_view_score(&v,123,902,11,0)==-1);
    assert(tile_request_outranks(&v,124,900,11,0,125,901,11,0));
    assert(!tile_request_outranks(&v,125,901,11,0,124,900,11,0));
    assert(tile_request_outranks(&v,124,900,11,0,126,900,11,1));
    assert(!tile_request_outranks(&v,126,900,11,1,124,900,11,0));
    v.gps_follow=0;
    assert(tile_view_score(&v,126,900,11,1)>=100);
    assert(tile_view_score(&v,126,900,11,0)==-1);
    v.start_x=-1;v.center_x=0;v.tiles_x=2;
    assert(tile_view_score(&v,2047,900,11,0)==1);
    assert(tile_view_score(&v,0,900,11,0)==0);
    assert(tile_view_score(&v,2046,900,11,0)==-1);
    v.valid=0;assert(tile_view_score(&v,0,900,11,0)==-1);
    puts("Live tile viewport priority tests passed");return 0;
}
