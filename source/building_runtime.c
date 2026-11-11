#include "building_runtime.h"
#include "network.h"
#include <3ds.h>
#include <curl/curl.h>
#include <jansson.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUILDING_JSON_LIMIT (512u*1024u)
#define PI 3.14159265358979323846
static struct {
    Thread thread;
    atomic_bool cancel,done;
    bool ok;
    TerrainGrid terrain;
    BuildingMesh mesh;
    double lat,lon,center_lat,center_lon;
    float span_m,origin_m;
    int zoom,tile_x,tile_y;
    char status[112];
} work;
static double degrees(double radians){return radians*180.0/PI;}
static double merc_y(double lat){
    return (1.0-asinh(tan(lat*PI/180.0))/PI)*0.5;
}
static float sample_elevation(double lat,double lon){
    double tiles=(double)(1<<work.zoom);
    double x=(lon+180.0)/360.0*tiles-work.tile_x;
    double y=merc_y(lat)*tiles-work.tile_y;
    x=fmax(0.0,fmin(32.0,x*32.0));
    y=fmax(0.0,fmin(32.0,y*32.0));
    int ix=(int)x,iy=(int)y;
    int nx=ix<32?ix+1:ix,ny=iy<32?iy+1:iy;
    float fx=(float)(x-ix),fy=(float)(y-iy);
    const float *a=work.terrain.height;
    float left=a[iy*33+ix]*(1-fx)+a[iy*33+nx]*fx;
    float right=a[ny*33+ix]*(1-fx)+a[ny*33+nx]*fx;
    return left*(1-fy)+right*fy;
}
static float tag_float(json_t *tags,const char *field,float fallback){
    json_t *v=json_object_get(tags,field);
    const char *s=json_string_value(v);
    if(!s)return fallback;
    char *end=NULL;
    double x=strtod(s,&end);
    if(end==s||!isfinite(x)||x<0 || x>1500)return fallback;
    while(*end==' ')end++;
    if(strcmp(end,"ft")==0 || strcmp(end,"feet")==0)x*=0.3048;
    return (float)x;
}
static void build_from_json(const unsigned char *bytes,u32 length){
    json_error_t err;
    json_t *root=json_loadb((const char*)bytes,length,0,&err);
    if(!root)return;
    json_t *elements=json_object_get(root,"elements");
    if(!json_is_array(elements)){json_decref(root);return;}
    size_t k;json_t *element;
    json_array_foreach(elements,k,element){
        if(atomic_load(&work.cancel) || work.mesh.buildings>=48)break;
        if(strcmp(json_string_value(json_object_get(element,"type"))?
                  json_string_value(json_object_get(element,"type")):"","way")!=0)
            continue;
        json_t *geometry=json_object_get(element,"geometry");
        size_t count=json_array_size(geometry);
        if(count<3||count>BUILDING_FOOTPRINT_MAX)continue;
        BuildingGeoPoint outline[BUILDING_FOOTPRINT_MAX];
        double avg_lat=0,avg_lon=0;bool good=true;
        for(size_t i=0;i<count;i++){
            json_t *point=json_array_get(geometry,i);
            json_t *la=json_object_get(point,"lat");
            json_t *lo=json_object_get(point,"lon");
            if(!json_is_number(la)||!json_is_number(lo)){good=false;break;}
            outline[i].lat=json_number_value(la);
            outline[i].lon=json_number_value(lo);
            avg_lat+=outline[i].lat;avg_lon+=outline[i].lon;
        }
        if(!good)continue;
        avg_lat/=count;avg_lon/=count;
        json_t *tags=json_object_get(element,"tags");
        float floors=tag_float(tags,"building:levels",2.5f);
        float height=tag_float(tags,"height",floors*3.0f);
        if(height<2 || height>1500)continue;
        float minimum=tag_float(tags,"min_height",0);
        if(minimum>height)minimum=0;
        float base=sample_elevation(avg_lat,avg_lon)+minimum;
        building_mesh_add(&work.mesh,outline,(unsigned)count,
            work.center_lat,work.center_lon,work.span_m,
            height-minimum,base,work.origin_m);
    }
    json_decref(root);
}
static void request_buildings(void *arg){
    (void)arg;
    void *h=network_alloc_tile_curl();
    u8 *bytes=NULL;u32 length=0;
    if(h && !atomic_load(&work.cancel)){
        char query[190],url[620];
        int n=snprintf(query,sizeof(query),
            "[out:json][timeout:12];way[\"building\"](around:220,%.6f,%.6f);out geom 60;",
            work.lat,work.lon);
        char *encoded=n>0&&n<(int)sizeof(query)?
            curl_easy_escape((CURL*)h,query,0):NULL;
        if(encoded){
            n=snprintf(url,sizeof(url),
                "https://overpass-api.de/api/interpreter?data=%s",encoded);
            curl_free(encoded);
            if(n>0&&n<(int)sizeof(url) &&
               network_request(h,url,BUILDING_JSON_LIMIT,&work.cancel,&bytes,&length) &&
               length>0 && !atomic_load(&work.cancel)){
                build_from_json(bytes,length);
            }
        }
    }
    free(bytes);
    if(h)network_free_tile_curl(h);
    work.ok=work.mesh.buildings>0 && !atomic_load(&work.cancel);
    atomic_store(&work.done,true);
}
void building_runtime_stop(void){
    if(work.thread){
        atomic_store(&work.cancel,true);
        threadJoin(work.thread,U64_MAX);
        threadFree(work.thread);work.thread=NULL;
    }
    atomic_store(&work.done,false);
}
bool building_runtime_start(double lat,double lon,
                             const TerrainGrid *terrain,float span_m,int zoom){
    building_runtime_stop();
    if(!terrain||!isfinite(lat)||!isfinite(lon)||fabs(lat)>85||
       fabs(lon)>180||!isfinite(span_m)||span_m<100||
       zoom<0||zoom>15||!is_connected()) {
        snprintf(work.status,sizeof(work.status),"Buildings unavailable at this location");
        return false;
    }
    memset(&work.mesh,0,sizeof(work.mesh));
    work.terrain=*terrain;
    work.lat=lat;work.lon=lon;work.span_m=span_m;work.zoom=zoom;
    terrain_web_tile(lat,lon,zoom,&work.tile_x,&work.tile_y);
    double n=(double)(1<<zoom);
    work.center_lon=((work.tile_x+0.5)/n)*360.0-180.0;
    work.center_lat=degrees(atan(sinh(PI*(1.0-2.0*(work.tile_y+0.5)/n))));
    work.origin_m=(terrain->min_height+terrain->max_height)*.5f;
    atomic_store(&work.cancel,false);atomic_store(&work.done,false);
    work.ok=false;
    work.thread=threadCreate(request_buildings,NULL,96*1024,0x32,-2,false);
    if(!work.thread){
        snprintf(work.status,sizeof(work.status),"Could not start building lookup");
        return false;
    }
    snprintf(work.status,sizeof(work.status),"Searching nearby OSM buildings...");
    return true;
}
const BuildingMesh *building_runtime_poll(void){
    if(!work.thread||!atomic_load(&work.done))return NULL;
    threadJoin(work.thread,U64_MAX);threadFree(work.thread);work.thread=NULL;
    if(work.ok)
        snprintf(work.status,sizeof(work.status),"%u buildings loaded",work.mesh.buildings);
    else snprintf(work.status,sizeof(work.status),"No buildings returned by provider");
    return work.ok?&work.mesh:NULL;
}
const char *building_runtime_status(void){return work.status;}
