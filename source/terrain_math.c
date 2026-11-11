#include "terrain_math.h"
#include <math.h>
#include <float.h>
#include <stddef.h>
float terrarium_height(uint8_t r,uint8_t g,uint8_t b){
    return 256.f*r+g+b*(1.f/256.f)-32768.f;
}
void terrain_web_tile(double lat,double lon,int zoom,int *x,int *y){
    if(!x||!y||zoom<0||zoom>15){if(x)*x=0;if(y)*y=0;return;}
    lat=fmax(-85.05112878,fmin(85.05112878,lat));
    lon=fmod(lon+180.,360.);if(lon<0)lon+=360.;
    double n=(double)(1<<zoom);
    *x=(int)floor(lon/360.*n);
    *y=(int)floor((1.-asinh(tan(lat*.017453292519943295))/3.141592653589793)*n*.5);
    if(*y<0)*y=0;
    if(*y>=n)*y=(int)n-1;
}
bool terrain_decode_grid(const uint8_t *rgb,int width,int height,int channels,TerrainGrid *out){
    if(!rgb||!out||width<33||width>512||height<33||height>512||channels<3||channels>4)return false;
    TerrainGrid temp={0};temp.min_height=FLT_MAX;temp.max_height=-FLT_MAX;
    for(int j=0;j<TERRAIN_GRID;j++)for(int i=0;i<TERRAIN_GRID;i++){
        /* Border sampling uses exactly the same edge texels in adjacent
           tiles, preventing gaps from mismatched interpolation. */
        int xx=i*(width-1)/(TERRAIN_GRID-1), yy=j*(height-1)/(TERRAIN_GRID-1);
        const uint8_t *p=rgb+((size_t)yy*width+xx)*channels;
        float h=terrarium_height(p[0],p[1],p[2]);
        if(!isfinite(h)||h< -13000||h>11000)return false;
        temp.height[j*TERRAIN_GRID+i]=h;
        if(h<temp.min_height)temp.min_height=h;
        if(h>temp.max_height)temp.max_height=h;
    }
    *out=temp;return true;
}
void terrain_world_vertex(const TerrainGrid *g,int i,int j,float span,float exaggeration,
                          float *x,float *y,float *z){
    if(!g||!x||!y||!z||i<0||i>=TERRAIN_GRID||j<0||j>=TERRAIN_GRID)return;
    if(!isfinite(span)||span<=0)span=1000;
    if(!isfinite(exaggeration)||exaggeration<0||exaggeration>10)exaggeration=1;
    *x=((float)i/32.f-.5f)*2.f;
    *z=((float)j/32.f-.5f)*2.f;
    *y=(g->height[j*TERRAIN_GRID+i]-(g->min_height+g->max_height)*.5f)*
       exaggeration*2.f/span;
}
