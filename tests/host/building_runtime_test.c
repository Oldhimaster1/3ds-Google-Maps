#include "building_runtime.h"
#include <3ds.h>
#include <stdatomic.h>
#include <curl/curl.h>
#include <jansson.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MockThread {int id;};
static int connected=1,response_ok=1,opened,closed;
static char request_url[640];
Thread threadCreate(void (*fn)(void*),void *arg,uint32_t stack,
                    int priority,int core,bool detached){
    (void)stack;(void)priority;(void)core;(void)detached;
    Thread t=malloc(sizeof(*t));assert(t);fn(arg);return t;
}
void threadJoin(Thread t,uint64_t timeout){(void)t;(void)timeout;}
void threadFree(Thread t){free(t);}
bool is_connected(void){return connected!=0;}
void *network_alloc_tile_curl(void){
    opened++;CURL *curl=curl_easy_init();assert(curl);return curl;
}
void network_free_tile_curl(void *p){
    closed++;curl_easy_cleanup((CURL*)p);
}
bool network_request(void *h,const char *url,size_t max,atomic_bool *cancel,
                     u8 **data,u32 *length){
    assert(h && url && max==512u*1024 && cancel && data && length);
    snprintf(request_url,sizeof(request_url),"%s",url);
    if(!response_ok)return false;
    *data=malloc(32);assert(*data);
    memset(*data,0,32);*length=32;return true;
}
/* A deterministic in-memory mock Jansson tree representing one OSM way.
 * Network and parser behavior are simulated; geometric extrusion is REAL. */
enum {ROOT=1,ARRAY,ELEMENT,GEOMETRY,POINT,LAT,LON,TAGS,TYPE,HEIGHT,LEVELS};
struct json_t {int kind,index;};
static json_t root={ROOT,0},array={ARRAY,0},element={ELEMENT,0};
static json_t geometry={GEOMETRY,0},tags={TAGS,0};
static json_t points[5]={{POINT,0},{POINT,1},{POINT,2},{POINT,3},{POINT,4}};
static json_t lat_value={LAT,0},lon_value={LON,0},type={TYPE,0};
static json_t height={HEIGHT,0},levels={LEVELS,0};
static const double lats[5]={40,40,40.0003,40.0003,40};
static const double lons[5]={-74,-73.9997,-73.9997,-74,-74};
static int current_index;
json_t *json_loadb(const char *text,size_t length,size_t flags,json_error_t *e){
    (void)text;(void)length;(void)flags;(void)e;return &root;
}
json_t *json_object_get(const json_t *v,const char *key){
    if(!v)return NULL;
    if(v->kind==ROOT && !strcmp(key,"elements"))return &array;
    if(v->kind==ELEMENT){
        if(!strcmp(key,"type"))return &type;
        if(!strcmp(key,"geometry"))return &geometry;
        if(!strcmp(key,"tags"))return &tags;
    }
    if(v->kind==POINT){
        current_index=v->index;
        if(!strcmp(key,"lat"))return &lat_value;
        if(!strcmp(key,"lon"))return &lon_value;
    }
    if(v->kind==TAGS){
        if(!strcmp(key,"height"))return &height;
        if(!strcmp(key,"building:levels"))return &levels;
    }
    return NULL;
}
int json_is_array(const json_t *v){
    return v && (v->kind==ARRAY||v->kind==GEOMETRY);
}
size_t json_array_size(const json_t *v){
    if(!v)return 0;
    if(v->kind==ARRAY)return 1;
    return v->kind==GEOMETRY?5:0;
}
json_t *json_array_get(const json_t *v,size_t i){
    if(!v)return NULL;
    if(v->kind==ARRAY && i==0)return &element;
    if(v->kind==GEOMETRY && i<5)return &points[i];
    return NULL;
}
int json_is_number(const json_t *v){
    return v && (v->kind==LAT||v->kind==LON);
}
double json_number_value(const json_t *v){
    return v->kind==LAT?lats[current_index]:lons[current_index];
}
const char *json_string_value(const json_t *v){
    if(!v)return NULL;
    if(v->kind==TYPE)return "way";
    if(v->kind==HEIGHT)return "12";
    if(v->kind==LEVELS)return "4";
    return NULL;
}
void json_decref(json_t *v){(void)v;}
int main(void){
    TerrainGrid grid={0};
    for(int i=0;i<TERRAIN_POINTS;i++)grid.height[i]=120;
    grid.min_height=grid.max_height=120;
    assert(!building_runtime_start(NAN,-74,&grid,7495,12));
    connected=0;
    assert(!building_runtime_start(40,-74,&grid,7495,12));
    connected=1;
    assert(building_runtime_start(40,-74,&grid,7495,12));
    const BuildingMesh *mesh=building_runtime_poll();
    assert(mesh && mesh->buildings==1);
    assert(mesh->vertex_count==20 && mesh->index_count==30);
    assert(fabsf(mesh->verts[0].y)<0.001f);
    assert(fabsf(mesh->verts[2].y-12.f*2.f/7495.f)<0.001f);
    assert(strstr(request_url,"overpass-api.de/api/interpreter?data="));
    assert(strstr(request_url,"way%5B%22building%22%5D")||
           strstr(request_url,"way%5b%22building%22%5d"));
    assert(building_runtime_poll()==NULL);
    response_ok=0;
    assert(building_runtime_start(40,-74,&grid,7495,12));
    assert(building_runtime_poll()==NULL);
    assert(strstr(building_runtime_status(),"No buildings"));
    building_runtime_stop();
    assert(opened==closed);
    puts("Live building worker: URL, terrain alignment, mesh, errors and cleanup passed");
    return 0;
}
