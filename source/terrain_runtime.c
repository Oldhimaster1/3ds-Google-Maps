#include "terrain_runtime.h"
#include "network.h"
#include "stb_image.h"
#include <3ds.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TERRAIN_DOWNLOAD_LIMIT (1024u*1024u)
static struct {
    Thread thread;
    atomic_bool cancel,done;
    bool ready,ok;
    TerrainGrid grid;
    float span_m;
    float focus_x,focus_z;
    double lat,lon;
    int z,x,y;
    char status[96];
} terrain;

static void fetch_terrain(void *arg) {
    (void)arg;
    bool ok=false;
    void *handle=NULL;
    u8 *buffer=NULL;
    u32 size=0;
    char url[192];
    int n=snprintf(url,sizeof(url),
        "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%d/%d/%d.png",
        terrain.z,terrain.x,terrain.y);
    if(n>0 && n<(int)sizeof(url) && !atomic_load(&terrain.cancel)) {
        handle=network_alloc_tile_curl();
        if(handle && network_request(handle,url,TERRAIN_DOWNLOAD_LIMIT,
                                   &terrain.cancel,&buffer,&size) &&
           size>=32 && !atomic_load(&terrain.cancel)) {
            int w=0,h=0,components=0;
            unsigned char *rgb=stbi_load_from_memory(
                  buffer,(int)size,&w,&h,&components,3);
            if(rgb && w==256 && h==256 && !atomic_load(&terrain.cancel)) {
                TerrainGrid decoded;
                if(terrain_decode_grid(rgb,w,h,3,&decoded)) {
                    terrain.grid=decoded;
                    ok=true;
                }
            }
            stbi_image_free(rgb);
        }
    }
    free(buffer);
    if(handle)network_free_tile_curl(handle);
    terrain.ok=ok && !atomic_load(&terrain.cancel);
    atomic_store(&terrain.done,true);
}

void terrain_runtime_stop(void) {
    if(terrain.thread){
        atomic_store(&terrain.cancel,true);
        threadJoin(terrain.thread,U64_MAX);
        threadFree(terrain.thread);
        terrain.thread=NULL;
    }
    terrain.ready=false;
    atomic_store(&terrain.done,false);
}
bool terrain_runtime_start(double lat,double lon,int zoom) {
    terrain_runtime_stop();
    if(!isfinite(lat)||!isfinite(lon)||fabs(lat)>85.05112878||
       fabs(lon)>180 || !is_connected()) {
        snprintf(terrain.status,sizeof(terrain.status),
                 "Elevation needs Wi-Fi and a valid map location");
        return false;
    }
    if(zoom<8)zoom=8;
    if(zoom>15)zoom=15;
    terrain.lat=lat;terrain.lon=lon;terrain.z=zoom;
    terrain_web_tile(lat,lon,zoom,&terrain.x,&terrain.y);
    /* Shift the local mesh so the exact selected map position, rather than
     * the middle of its Mercator tile, is underneath the scene's orbit camera. */
    double longitude=lon==180.0?-180.0:lon;
    double ntiles=(double)(1<<zoom);
    double fx=(longitude+180.0)/360.0*ntiles-terrain.x;
    double fy=(1.0-asinh(tan(lat*.017453292519943295))/
                3.141592653589793)*.5*ntiles-terrain.y;
    terrain.focus_x=(float)((fx-.5)*2.0);
    terrain.focus_z=(float)((fy-.5)*2.0);
    terrain.span_m=(float)(40075016.686*cos(lat*0.017453292519943295)/
                            (double)(1<<zoom));
    if(!isfinite(terrain.span_m)||terrain.span_m<100)terrain.span_m=100;
    terrain.ok=false;
    atomic_store(&terrain.cancel,false);
    atomic_store(&terrain.done,false);
    terrain.thread=threadCreate(fetch_terrain,NULL,96*1024,0x31,-2,false);
    if(!terrain.thread){
        snprintf(terrain.status,sizeof(terrain.status),
                 "Could not start elevation download");
        return false;
    }
    snprintf(terrain.status,sizeof(terrain.status),
             "Loading elevation for selected map position...");
    return true;
}
bool terrain_runtime_poll(TerrainGrid *out,float *span_m,
                          float *focus_x,float *focus_z) {
    if(!terrain.thread||!atomic_load(&terrain.done))return false;
    threadJoin(terrain.thread,U64_MAX);
    threadFree(terrain.thread);
    terrain.thread=NULL;
    terrain.ready=terrain.ok;
    if(terrain.ok)snprintf(terrain.status,sizeof(terrain.status),
                           "Live terrain loaded");
    else snprintf(terrain.status,sizeof(terrain.status),
                  "Elevation unavailable here; no demo location substituted");
    if(!terrain.ready||!out||!span_m||!focus_x||!focus_z)return false;
    *out=terrain.grid;
    *span_m=terrain.span_m;
    *focus_x=terrain.focus_x;
    *focus_z=terrain.focus_z;
    terrain.ready=false;
    return true;
}
const char *terrain_runtime_status(void){return terrain.status;}
