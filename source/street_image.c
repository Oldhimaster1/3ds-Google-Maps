#include "street_image.h"
unsigned street_image_tiled_index(unsigned x, unsigned y) {
    unsigned m=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|
               ((x&4)<<2)|((y&4)<<3);
    return ((y>>3)*128+(x>>3))*64+m;
}
static uint16_t pack(const unsigned char *c) {
    return (uint16_t)(((c[0]>>3)<<11)|((c[1]>>2)<<5)|(c[2]>>3));
}
void street_google_write_half_rgb565(const unsigned char *rgb, unsigned half,
                                      uint16_t *target) {
    if (!rgb || !target || half>1) return;
    for (unsigned y=0;y<512;y++) {
        const unsigned char *row=rgb+(511-y)*512*3;
        for(unsigned x=0;x<512;x++)
            target[street_image_tiled_index(x+half*512,y)]=pack(row+x*3);
    }
}
