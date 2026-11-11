#include "detail_runtime.h"
#include "detail_plan.h"
#include <3ds.h>
#include <curl/curl.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct MockThread{void (*fn)(void*);void *arg;int executed;};
static Thread active;
static u64 clock_ms=1000;
static int connected=1,base_busy=0,network_ok=1,uploads,clears,requests,created,freed;
static int last_side;
static DetailKey last_uploaded;
static char last_url[400];

u64 osGetTime(void){return clock_ms;}
Thread threadCreate(void (*fn)(void*),void *arg,uint32_t stack,int priority,
                    int core,bool detached){
 (void)priority;(void)core;(void)detached;
 assert(stack<=128*1024&&stack>=64*1024);
 assert(!active);Thread t=calloc(1,sizeof(*t));assert(t);
 t->fn=fn;t->arg=arg;active=t;created++;return t;
}
static void run_active(void){
 assert(active && !active->executed);
 active->executed=1;active->fn(active->arg);
}
void threadJoin(Thread t,u64 timeout){
 (void)timeout;
 assert(t==active);
 if(!t->executed)run_active();
}
void threadFree(Thread t){assert(t==active);active=NULL;free(t);freed++;}
bool is_connected(void){return connected!=0;}
bool pano_runtime_network_active(void){return base_busy!=0;}
void *network_alloc_tile_curl(void){
 struct MockCurl *h=calloc(1,sizeof(*h));assert(h);return h;
}
void network_free_tile_curl(void *h){free(h);}
bool network_request(void *h,const char *url,size_t max,atomic_bool *cancel,
                     u8 **bytes,u32 *length){
 assert(h&&url&&cancel&&bytes&&length&&max==1024u*1024u);
 requests++;snprintf(last_url,sizeof(last_url),"%s",url);
 last_side=strstr(url,"gibs.earthdata.nasa.gov")?256:512;
 if(!network_ok||atomic_load(cancel))return false;
 *bytes=malloc(128);assert(*bytes);memset(*bytes,0x7f,128);*length=128;
 return true;
}
int stbi_info_from_memory(const unsigned char *data,int length,int *w,int *h,int *c){
 assert(data && length==128);*w=*h=last_side;*c=3;return 1;
}
unsigned char *stbi_load_from_memory(const unsigned char *data,int length,
                                    int *w,int *h,int *channels,int requested){
 assert(data && length==128 && requested==3 && *w==last_side && *h==last_side);
 *channels=3;
 unsigned n=(unsigned)last_side*(unsigned)last_side*3;
 unsigned char *rgb=malloc(n);assert(rgb);
 for(unsigned i=0;i<n;i++)rgb[i]=(unsigned char)(i*37u);
 return rgb;
}
void stbi_image_free(void *p){free(p);}
bool explore_detail_upload(DetailKey k,const uint16_t *pixels){
 assert(pixels && (k.kind==DETAIL_PANORAMA||k.kind==DETAIL_GLOBE));
 assert(k.kind==DETAIL_PANORAMA?k.z>=2:k.z<=8);
 assert(pixels[0]!=pixels[1]);uploads++;last_uploaded=k;return true;
}
void explore_detail_clear(void){clears++;}
static PanoLiveImage google(void){
 PanoLiveImage p={0};p.provider=STREET_PROVIDER_GOOGLE;p.is_pano=true;
 snprintf(p.id,sizeof(p.id),"1234567890123456789012");return p;
}
static void advance(u64 delta){clock_ms+=delta;}
int main(void){
 PanoLiveImage pano=google();
 detail_runtime_update_globe(40,-74,2.0f,2);
 assert(requests==0 && !active);
 advance(240);detail_runtime_update_globe(40,-74,2.0f,2);
 assert(active&&requests==0);
 run_active();advance(1);detail_runtime_update_globe(40,-74,2.0f,2);
 assert(!active&&requests==1&&uploads==1&&last_uploaded.kind==DETAIL_GLOBE);
 assert(strstr(last_url,"gibs.earthdata.nasa.gov"));
 advance(500);detail_runtime_update_globe(40,-74,2.0f,2);
 assert(uploads==1&&requests==1);
 advance(1);detail_runtime_update_globe(40,50,2.0f,2);
 advance(230);detail_runtime_update_globe(40,50,2.0f,2);
 assert(active);
 /* In flight request from old view is discarded, not rendered over panorama. */
 detail_runtime_update_panorama(&pano,0,0,55,3);
 assert(active);
 run_active();advance(1);detail_runtime_update_panorama(&pano,0,0,55,3);
 assert(!active&&uploads==1);
 advance(230);detail_runtime_update_panorama(&pano,0,0,55,3);
 assert(active);run_active();advance(1);
 detail_runtime_update_panorama(&pano,0,0,55,3);
 assert(!active&&uploads==2&&last_uploaded.kind==DETAIL_PANORAMA);
 assert(strstr(last_url,"panoid=") && strstr(last_url,"zoom=2"));
 /* A deliberately canceled image is NOT a failed provider request. */
 advance(2);detail_runtime_update_panorama(&pano,2.5f,0,55,3);
 advance(230);detail_runtime_update_panorama(&pano,2.5f,0,55,3);
 assert(active);
 detail_runtime_update_panorama(&pano,-2.5f,0,55,3);
 detail_runtime_update_panorama(&pano,2.5f,0,55,3);
 run_active();advance(1);detail_runtime_update_panorama(&pano,2.5f,0,55,3);
 assert(!active && uploads==2);
 advance(230);detail_runtime_update_panorama(&pano,2.5f,0,55,3);
 assert(active);run_active();advance(1);
 detail_runtime_update_panorama(&pano,2.5f,0,55,3);
 assert(uploads==3);
 base_busy=1;advance(1);
 detail_runtime_update_panorama(&pano,0,0,28,2);
 advance(230);detail_runtime_update_panorama(&pano,0,0,28,2);
 assert(!active);
 base_busy=0;detail_runtime_update_panorama(&pano,0,0,28,2);
 assert(active);run_active();advance(1);
 detail_runtime_update_panorama(&pano,0,0,28,2);
 assert(uploads==4&&last_uploaded.z==4);
 int old_requests=requests;
 detail_runtime_update_panorama(&pano,0,0,55,0);
 assert(!active);
 advance(1000);detail_runtime_update_panorama(&pano,0,0,55,0);
 assert(requests==old_requests);
 network_ok=0;
 detail_runtime_update_globe(0,0,2.1f,1);
 advance(230);detail_runtime_update_globe(0,0,2.1f,1);
 assert(active);run_active();advance(1);detail_runtime_update_globe(0,0,2.1f,1);
 assert(uploads==4);
 int failed_requests=requests;
 advance(500);detail_runtime_update_globe(0,0,2.1f,1);
 assert(requests==failed_requests);
 detail_runtime_stop();
 assert(!active&&created==freed);
 connected=0;detail_runtime_update_globe(0,0,2.0f,2);
 advance(500);detail_runtime_update_globe(0,0,2.0f,2);
 assert(requests==failed_requests);
 detail_runtime_stop();
 puts("detail runtime: PASS");
 return 0;
}
