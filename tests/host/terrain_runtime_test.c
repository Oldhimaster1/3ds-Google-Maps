#include "terrain_runtime.h"
#include <3ds.h>
#include <stdatomic.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct MockThread{int value;};
static int network_on=1,request_ok=1,image_ok=1,bad_size=0,opened,closed,requests;
static char requested[192];
Thread threadCreate(void (*func)(void *),void *arg,uint32_t stack,int priority,int core,bool detached){
    (void)stack;(void)priority;(void)core;(void)detached;
    Thread t=malloc(sizeof(*t));assert(t);
    func(arg);return t;
}
void threadJoin(Thread t,uint64_t timeout){(void)t;(void)timeout;}
void threadFree(Thread t){free(t);}
bool is_connected(void){return network_on!=0;}
void *network_alloc_tile_curl(void){opened++;return (void*)1;}
void network_free_tile_curl(void *h){assert(h);closed++;}
bool network_request(void *h,const char *url,size_t limit,atomic_bool *cancel,
                     u8 **buffer,u32 *size){
    assert(h && url && limit>=32 && cancel && buffer && size);
    requests++;
    snprintf(requested,sizeof(requested),"%s",url);
    if(!request_ok)return false;
    *buffer=malloc(32);assert(*buffer);
    memset(*buffer,0xAA,32);*size=32;return true;
}
unsigned char *stbi_load_from_memory(const unsigned char *bytes,int len,
                                    int *width,int *height,int *comp,int desired){
    assert(bytes&&len==32&&desired==3);
    if(!image_ok)return NULL;
    *width=bad_size?255:256;*height=256;*comp=3;
    unsigned char *p=malloc(256*256*3);assert(p);
    for(int i=0;i<256*256;i++){p[i*3]=128;p[i*3+1]=120;p[i*3+2]=0;}
    return p;
}
void stbi_image_free(void *p){free(p);}
int main(void){
    TerrainGrid grid;float meters;
    assert(!terrain_runtime_start(91.0,0.0,10));
    assert(!terrain_runtime_start(NAN,0,10));
    network_on=0;assert(!terrain_runtime_start(40,-74,12));
    network_on=1;assert(terrain_runtime_start(36.09,-112.13,11));
    assert(requests==1 && strstr(requested,"/terrarium/11/")!=NULL);
    assert(terrain_runtime_poll(&grid,&meters));
    assert(grid.min_height==120 && grid.max_height==120);
    assert(meters>100 && meters<40000);
    assert(!terrain_runtime_poll(&grid,&meters));
    assert(opened==closed);
    assert(terrain_runtime_start(0,179.99,10));
    assert(strstr(requested,"/terrarium/10/1023/")!=NULL);
    terrain_runtime_stop();
    request_ok=0;assert(terrain_runtime_start(40,-73,12));
    assert(!terrain_runtime_poll(&grid,&meters));
    assert(strstr(terrain_runtime_status(),"unavailable"));
    request_ok=1;bad_size=1;
    assert(terrain_runtime_start(40,-73,12));
    assert(!terrain_runtime_poll(&grid,&meters));
    bad_size=0;image_ok=0;
    assert(terrain_runtime_start(40,-73,12));
    assert(!terrain_runtime_poll(&grid,&meters));
    terrain_runtime_stop();
    assert(opened==closed);
    puts("Live terrain worker: location, URL, decoding, failures, cleanup passed");
    return 0;
}
