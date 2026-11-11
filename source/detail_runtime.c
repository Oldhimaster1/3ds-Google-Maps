#include "detail_runtime.h"
#include "detail_plan.h"
#include "explore.h"
#include "network.h"
#include "pano_runtime.h"
#include "stb_image.h"
#include <3ds.h>
#include <curl/curl.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only one 256 or 512 pixel RGB565 tile in GPU memory. The original 1024x512
 * panorama and bundled globe always remain visible as fallbacks. Worker owns
 * JPEG/PNG and RGB allocations; the render thread alone creates GPU textures. */
#define DETAIL_COMPRESSED_MAX (1024u*1024u)
#define DETAIL_STABLE_MS 220u
#define DETAIL_RETRY_MS 30000u
static struct {
 Thread thread;
 atomic_bool cancel,done;
 bool result_ok,desired_valid,loaded_valid,failed_valid;
 DetailKind context;
 DetailKey desired,requested,loaded,failed;
 u64 desired_since,last_request,failed_since;
 char current_id[48],requested_id[48],status[100];
 uint16_t *pixels;
} stream;

static unsigned tiled_index(unsigned x,unsigned y,unsigned width){
 unsigned m=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|
            ((x&4)<<2)|((y&4)<<3);
 return ((y>>3)*(width>>3)+(x>>3))*64+m;
}
static void fetch_detail(void *ignored){
 (void)ignored;
 u8 *bytes=NULL;u32 length=0;
 void *handle=NULL;uint16_t *pixels=NULL;
 unsigned char *rgb=NULL;
 char url[256];int w=0,h=0,comp=0;
 DetailKey key=stream.requested;
 unsigned side=key.kind==DETAIL_PANORAMA?512u:256u;
 bool good=false;
 if(!atomic_load(&stream.cancel) &&
    detail_plan_url(key,stream.requested_id,url,sizeof(url))){
  handle=network_alloc_tile_curl();
  if(handle){
   CURL *curl=(CURL *)handle;
   curl_easy_setopt(curl,CURLOPT_TIMEOUT,8L);
   curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,3L);
   curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
   /* Consistent with the experimental Google tile path in pano_runtime.c.
    * NASA requests identify the homebrew app without spoofing a browser. */
   curl_easy_setopt(curl,CURLOPT_USERAGENT,key.kind==DETAIL_PANORAMA?
     "Mozilla/5.0 (Windows NT 11.0; Win64; x64; rv:151.0) Gecko/20100101 Firefox/151.0":
     "3DS-Google-Maps/2.1 (+https://github.com/Oldhimaster1/3ds-Google-Maps)");
   if(network_request(handle,url,DETAIL_COMPRESSED_MAX,&stream.cancel,&bytes,&length) &&
      !atomic_load(&stream.cancel) && length>100 &&
      stbi_info_from_memory(bytes,(int)length,&w,&h,&comp) &&
      w==(int)side && h==(int)side){
    rgb=stbi_load_from_memory(bytes,(int)length,&w,&h,&comp,3);
    if(rgb){
     pixels=malloc(side*side*sizeof(*pixels));
     if(pixels){
      good=true;
      for(unsigned y=0;y<side;y++){
       if(atomic_load(&stream.cancel)){good=false;break;}
       const unsigned char *row=rgb+(side-1-y)*side*3;
       for(unsigned x=0;x<side;x++){
        const unsigned char *c=row+3*x;
        pixels[tiled_index(x,y,side)]=(uint16_t)
          (((c[0]>>3)<<11)|((c[1]>>2)<<5)|(c[2]>>3));
       }
      }
     }
    }
   }
  }
 }
 free(bytes);
 if(rgb)stbi_image_free(rgb);
 if(handle)network_free_tile_curl(handle);
 if(!good||atomic_load(&stream.cancel)){free(pixels);pixels=NULL;}
 stream.pixels=pixels;
 stream.result_ok=pixels!=NULL;
 atomic_store(&stream.done,true);
}
static void poll_finished(void){
 if(!stream.thread||!atomic_load(&stream.done))return;
 threadJoin(stream.thread,U64_MAX);threadFree(stream.thread);stream.thread=NULL;
 bool current=stream.desired_valid &&
  stream.context==stream.requested.kind &&
  detail_plan_equal(stream.desired,stream.requested) &&
  (stream.context!=DETAIL_PANORAMA ||
   !strcmp(stream.current_id,stream.requested_id));
 if(stream.result_ok&&stream.pixels&&current &&
    explore_detail_upload(stream.requested,stream.pixels)){
  stream.loaded=stream.requested;stream.loaded_valid=true;
  stream.failed_valid=false;
  snprintf(stream.status,sizeof(stream.status),
   "%s detail z%u loaded",
   stream.context==DETAIL_PANORAMA?"Panorama":"NASA globe",
   (unsigned)stream.requested.z);
 }else if(current){
  stream.failed=stream.requested;stream.failed_valid=true;
  stream.failed_since=osGetTime();
  snprintf(stream.status,sizeof(stream.status),
   "%s detail unavailable; base image retained",
   stream.context==DETAIL_PANORAMA?"Panorama":"Globe");
 }
 free(stream.pixels);stream.pixels=NULL;
 atomic_store(&stream.done,false);
}
static void reset_context(DetailKind kind,const char *id){
 if(stream.context==kind &&
    (kind!=DETAIL_PANORAMA || (id&&!strcmp(stream.current_id,id))))return;
 if(stream.thread)atomic_store(&stream.cancel,true);
 explore_detail_clear();
 stream.context=kind;
 stream.desired_valid=stream.loaded_valid=stream.failed_valid=false;
 stream.desired_since=0;stream.last_request=0;
 snprintf(stream.current_id,sizeof(stream.current_id),"%s",id?id:"");
 stream.status[0]=0;
}
static void tick(DetailKey wanted){
 u64 now=osGetTime();
 poll_finished();
 if(!stream.desired_valid || !detail_plan_equal(stream.desired,wanted)){
  stream.desired=wanted;stream.desired_valid=true;stream.desired_since=now;
  if(stream.thread && !detail_plan_equal(stream.requested,wanted))
   atomic_store(&stream.cancel,true);
 }
 if(stream.thread||(stream.loaded_valid&&detail_plan_equal(stream.loaded,wanted))||
    (stream.failed_valid&&detail_plan_equal(stream.failed,wanted)&&
    now>=stream.failed_since&&now-stream.failed_since<DETAIL_RETRY_MS)||
    !is_connected()||now<stream.desired_since||
    now-stream.desired_since<DETAIL_STABLE_MS||
    now<stream.last_request||now-stream.last_request<DETAIL_STABLE_MS)return;
 /* A panorama upgrade takes precedence over extra detail. Never compete with
  * the existing preview and zoom-1 workers for 3DS CPU/network resources. */
 if(stream.context==DETAIL_PANORAMA && pano_runtime_network_active())return;
 stream.requested=wanted;
 snprintf(stream.requested_id,sizeof(stream.requested_id),"%s",stream.current_id);
 stream.result_ok=false;stream.pixels=NULL;
 atomic_store(&stream.cancel,false);atomic_store(&stream.done,false);
 stream.thread=threadCreate(fetch_detail,NULL,96*1024,0x32,-2,false);
 if(stream.thread){
  stream.last_request=now;
  snprintf(stream.status,sizeof(stream.status),"%s detail z%u downloading...",
   stream.context==DETAIL_PANORAMA?"Panorama":"NASA globe",
   (unsigned)wanted.z);
 }
}
void detail_runtime_update_panorama(const PanoLiveImage *p,float yaw,float pitch,
                                    float fov,unsigned quality){
 DetailKey wanted;
 if(!p||p->provider!=STREET_PROVIDER_GOOGLE||!p->is_pano||
    !detail_plan_panorama(yaw,pitch,fov,quality,&wanted) ||
    !detail_plan_url(wanted,p->id,(char[256]){0},256)){
  if(stream.context!=DETAIL_NONE)reset_context(DETAIL_NONE,NULL);
  poll_finished();
  return;
 }
 reset_context(DETAIL_PANORAMA,p->id);
 tick(wanted);
}
void detail_runtime_update_globe(double lat,double lon,float distance,unsigned quality){
 DetailKey wanted;
 if(!detail_plan_globe(lat,lon,distance,quality,&wanted)){
  if(stream.context!=DETAIL_NONE)reset_context(DETAIL_NONE,NULL);
  poll_finished();
  return;
 }
 reset_context(DETAIL_GLOBE,NULL);
 tick(wanted);
}
void detail_runtime_stop(void){
 if(stream.thread){
  atomic_store(&stream.cancel,true);
  threadJoin(stream.thread,U64_MAX);
  threadFree(stream.thread);
  stream.thread=NULL;
 }
 free(stream.pixels);stream.pixels=NULL;
 explore_detail_clear();
 stream.context=DETAIL_NONE;
 stream.desired_valid=stream.loaded_valid=stream.failed_valid=false;
 stream.desired_since=stream.last_request=0;
 stream.current_id[0]=stream.requested_id[0]=stream.status[0]=0;
 atomic_store(&stream.cancel,false);atomic_store(&stream.done,false);
}
const char *detail_runtime_status(void){
 return stream.status[0]?stream.status:"Detail waits for stable view / Wi-Fi";
}
