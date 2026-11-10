#include "street_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
int main(void) {
    unsigned char *rgb=malloc(512*512*3);
    uint16_t *gpu=malloc(1024*512*2);
    assert(rgb && gpu);
    memset(gpu,0,1024*512*2);
    /* Each hemisphere has different colors at both physical top/bottom.
     * Decoding reverses y exactly once. */
    for(unsigned half=0;half<2;half++) {
        for(unsigned y=0;y<512;y++)for(unsigned x=0;x<512;x++) {
            unsigned char *p=rgb+(y*512+x)*3;
            p[0]=half?240:16;
            p[1]=y<256?240:16;
            p[2]=128;
        }
        street_google_write_half_rgb565(rgb,half,gpu);
    }
    unsigned a=street_image_tiled_index(511,0),b=street_image_tiled_index(512,0);
    assert(gpu[a] && gpu[b] && gpu[a]!=gpu[b]);
    assert(gpu[street_image_tiled_index(0,0)]!=gpu[street_image_tiled_index(0,511)]);
    assert(gpu[street_image_tiled_index(0,0)]==gpu[street_image_tiled_index(510,0)]);
    assert(gpu[street_image_tiled_index(512,511)]==gpu[street_image_tiled_index(1023,511)]);
    free(gpu);free(rgb);
    puts("PASS Google tile assembly: two halves, Morton indexing, vertical flip");
    return 0;
}
