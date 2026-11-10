#include "street_look_input.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void) {
    float x=8,y=9;
    street_look_axes(0,0,&x,&y); assert(x==0 && y==0);
    street_look_axes(14,-15,&x,&y); assert(x==0 && y==0);
    street_look_axes(120,25,&x,&y); assert(x>.3f && y==0);
    street_look_axes(-123,-26,&x,&y); assert(x<-.3f && y==0);
    street_look_axes(26,125,&x,&y); assert(x==0 && y>.3f);
    street_look_axes(125,105,&x,&y); assert(x>0 && y>0);
    street_look_axes(-100,-100,&x,&y); assert(x<0 && y<0);
    street_look_axes(22,3,&x,&y); assert(x>=0 && y==0);
    printf("PASS street input 8-axis test cases\n");
    return 0;
}
