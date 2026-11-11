#include "tile_priority.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t rnd=0xC0FFEE;
static uint32_t rand32(void){rnd^=rnd<<13;rnd^=rnd>>17;rnd^=rnd<<5;return rnd;}
int main(void){
    for(int i=0;i<200000;i++){
        int zoom=2+(int)(rand32()%17),world=1<<zoom;
        int x=(int)(rand32()%(unsigned)world);
        int y=3+(int)(rand32()%(unsigned)(world-3));
        TileViewport v={.zoom=zoom,.world=world,.start_x=x-1,.start_y=y-1,
            .tiles_x=3,.tiles_y=3,.center_x=x,.center_y=y,
            .gps_follow=(int)(rand32()%2),.valid=1};
        assert(tile_view_score(&v,x,y,zoom,0)==0);
        assert(tile_view_score(&v,x-1,y,zoom,0)>=0);
        assert(tile_view_score(&v,tile_wrap(x-1,world),y,zoom,0)>=0);
        assert(tile_view_score(&v,x,y,zoom-1,0)==-1);
        assert(tile_request_outranks(&v,x,y,zoom,0,tile_wrap(x-1,world),y,zoom,0));
        assert(!tile_request_outranks(&v,x,y,zoom,1,x,y,zoom,0));
        int score=tile_view_score(&v,x+2,y,zoom,1);
        if(v.gps_follow)assert(score==-1);
        else assert(score>=100);
    }
    puts("200,000 randomized GPS viewport/priority cases passed");
    return 0;
}
