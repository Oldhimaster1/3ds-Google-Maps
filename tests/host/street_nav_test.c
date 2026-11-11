#include "street_nav.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    PanoLiveImage p = {0};
    p.link_count=4; p.azimuth=0;
    const float bearings[] = { 0, 90, 180, 270 };
    for (unsigned i=0;i<4;i++) {
        snprintf(p.links[i].id,sizeof(p.links[i].id),"node%u",i);
        p.links[i].bearing=bearings[i];
    }
    assert(street_nav_forward(&p,0.f)==0);
    assert(street_nav_forward(&p,(float)M_PI/2)==1);
    assert(street_nav_forward(&p,(float)M_PI)==2);
    assert(street_nav_backward(&p)==2);
    assert(street_nav_at_cell(&p,0,1)==0);
    assert(street_nav_at_cell(&p,0,5)==1);
    assert(street_nav_at_cell(&p,0,7)==2);
    assert(street_nav_at_cell(&p,0,3)==3);
    assert(street_nav_at_cell(&p,0,4)==-1);
    assert(fabsf(street_nav_bearing(40,-74,41,-74))<.001f);
    assert(fabsf(street_nav_bearing(40,-74,40,-73)-90.f)<1.f);
    assert(street_nav_cell_for_bearing(315.f,0.f)==0);
    p.link_count=0;
    assert(street_nav_forward(&p,0)==-1);
    puts("PASS navigation: forward, back, touch sectors, bearing, empty links");
    return 0;
}
