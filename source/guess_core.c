#include "guess_core.h"
#include <math.h>
#include <string.h>
#define PI 3.1415926535897932384626433832795
#define DEG (PI/180.0)
/* These are broad geographic sampling AREAS, not a list of panorama IDs or
 * spawn coordinates. Live Google validation decides if any coordinate works.
 * A full road-centerline pack will be preferable before a public release. */
typedef struct { double south,west,north,east; unsigned weight; unsigned scope; } Area;
#define W 1
#define U 2
#define E 4
static const Area areas[]={
 {24,-125,49,-66,9,W|U}, /* Continental US */
 {49,-125,60,-115,2,W}, {43,-80,53,-60,3,W}, /* Canada */
 {15,-110,33,-85,3,W}, {7,-85,20,-59,4,W}, /* Central America */
 {-34,-73,6,-34,7,W}, {-55,-75,-20,-57,3,W}, {-5,-81,12,-67,4,W},
 {50,-11,59,2,4,W|E}, {42,-5,52,9,6,W|E}, {46,5,56,20,8,W|E},
 {54,10,70,32,3,W|E}, {36,-10,46,30,5,W|E}, {45,19,56,43,5,W|E},
 {36,25,44,44,3,W|E}, {55,28,66,51,3,W|E},
 {29,-12,37,12,3,W}, {5,-17,17,16,4,W}, {-3,8,15,31,4,W},
 {-18,11,-2,35,4,W}, {-35,16,-21,34,4,W},
 {28,66,41,88,3,W}, {8,68,27,88,6,W}, {21,88,35,98,4,W},
 {5,95,21,108,5,W}, {-9,99,8,123,6,W}, {-8,115,6,133,4,W},
 {0,99,20,123,5,W}, {20,102,37,122,6,W}, {30,112,44,134,5,W},
 {30,126,44,146,6,W}, {33,125,40,131,3,W},
 {31,31,42,51,3,W}, {20,42,33,61,3,W},
 {-44,113,-11,155,6,W}, {-46,166,-34,179,3,W},
 {-25,43,-12,51,2,W}, {-10,113,6,160,3,W},
 {28,96,38,109,2,W}, {38,68,55,91,3,W},
 {37,-10,44,5,2,W|E}, {44,7,51,18,3,W|E},
};
static uint64_t next64(uint64_t *state){
    uint64_t z=(*state+=UINT64_C(0x9e3779b97f4a7c15));
    z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
    z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);
    return z^(z>>31);
}
static double unit(uint64_t *state){return (double)(next64(state)>>11)*(1.0/9007199254740992.0);}
static double wrap_lon(double lon){
    lon=fmod(lon+180.0,360.0);
    if(lon<0)lon+=360.0;
    return lon-180.0;
}
static double clamp_lat(double lat){return fmax(-85.05112878,fmin(85.05112878,lat));}
static bool valid(GuessPoint p){return isfinite(p.lat)&&isfinite(p.lon)&&fabs(p.lat)<=85.05112878&&fabs(p.lon)<=180;}
void guess_init(GuessGame *g,uint64_t seed){
    if(!g)return;
    memset(g,0,sizeof(*g));
    g->rng=seed?seed:UINT64_C(0x12345feed83ca917);
    g->region=GUESS_WORLD;g->rules=GUESS_MOVING;g->phase=GUESS_MENU;
}
void guess_begin(GuessGame *g,GuessRegion r,GuessRules rules,unsigned seconds){
    if(!g)return;
    if(r<GUESS_WORLD||r>=GUESS_MAP_COUNT)r=GUESS_WORLD;
    if(rules<GUESS_MOVING||rules>=GUESS_RULE_COUNT)rules=GUESS_MOVING;
    g->region=r;g->rules=rules;g->round=0;g->attempt=0;
    g->time_limit_ms=seconds>600?600000u:seconds*1000u;
    g->remaining_ms=g->time_limit_ms;g->last_tick=0;
    g->total=0;g->has_pin=false;g->phase=GUESS_FINDING;
    memset(g->score,0,sizeof(g->score));memset(g->distances_km,0,sizeof(g->distances_km));
}
bool guess_random_candidate(GuessGame *g,GuessPoint *out){
    if(!g||!out||g->phase!=GUESS_FINDING||g->attempt>=GUESS_MAX_CANDIDATE_TRIES)return false;
    ++g->attempt;
    const unsigned mask=g->region==GUESS_USA?U:g->region==GUESS_EUROPE?E:W;
    unsigned weight=0;
    for(unsigned i=0;i<sizeof(areas)/sizeof(areas[0]);++i)
        if(areas[i].scope&mask)weight+=areas[i].weight;
    if(!weight)return false;
    unsigned pick=(unsigned)(next64(&g->rng)%weight);
    const Area *a=NULL;
    for(unsigned i=0;i<sizeof(areas)/sizeof(areas[0]);++i){
        if(!(areas[i].scope&mask))continue;
        if(pick<areas[i].weight){a=&areas[i];break;}
        pick-=areas[i].weight;
    }
    if(!a)return false;
    out->lat=a->south+(a->north-a->south)*unit(&g->rng);
    out->lon=a->west+(a->east-a->west)*unit(&g->rng);
    return valid(*out);
}
double guess_distance_km(GuessPoint a,GuessPoint b){
    if(!valid(a)||!valid(b))return INFINITY;
    double lat=(b.lat-a.lat)*DEG,lon=wrap_lon(b.lon-a.lon)*DEG;
    double t=sin(lat/2)*sin(lat/2)+cos(a.lat*DEG)*cos(b.lat*DEG)*sin(lon/2)*sin(lon/2);
    t=fmax(0.0,fmin(1.0,t));
    return 12742.0088*atan2(sqrt(t),sqrt(1.0-t));
}
bool guess_candidate_acceptable(const GuessGame *g,GuessPoint p){
    if(!g||!valid(p))return false;
    if(g->region==GUESS_USA && (p.lat<24||p.lat>49||p.lon < -125||p.lon> -66))return false;
    if(g->region==GUESS_EUROPE && (p.lat<36||p.lat>70||p.lon < -11||p.lon>51))return false;
    for(unsigned i=0;i<g->history_size;i++)
        if(guess_distance_km(g->history[i],p)<35.0)return false;
    return true;
}
bool guess_accept_start(GuessGame *g,GuessPoint p,uint64_t now_ms){
    if(!g||g->phase!=GUESS_FINDING||!guess_candidate_acceptable(g,p))return false;
    g->start=p;g->has_pin=false;g->remaining_ms=g->time_limit_ms;
    g->last_tick=now_ms;g->phase=GUESS_PLAYING;
    g->history[g->history_pos]=p;
    g->history_pos=(g->history_pos+1)%GUESS_HISTORY;
    if(g->history_size<GUESS_HISTORY)g->history_size++;
    return true;
}
void guess_set_pin(GuessGame *g,GuessPoint p){
    if(g&&g->phase==GUESS_PLAYING&&valid(p)){g->pin=p;g->has_pin=true;}
}
unsigned guess_score_km(double d,GuessRegion r){
    if(!isfinite(d)||d<0)return 0;
    if(d<=0.15)return 5000;
    double scale=r==GUESS_WORLD?1492.7:r==GUESS_USA?420.0:290.0;
    double val=5000.0*exp(-d/scale);
    if(val<0.5)return 0;
    if(val>5000)return 5000;
    return (unsigned)floor(val+0.5);
}
bool guess_submit(GuessGame *g){
    if(!g||g->phase!=GUESS_PLAYING)return false;
    if(!g->has_pin){g->pin=(GuessPoint){0,0};}
    double d=g->has_pin?guess_distance_km(g->start,g->pin):INFINITY;
    g->distances_km[g->round]=d;
    unsigned score=g->has_pin?guess_score_km(d,g->region):0;
    g->score[g->round]=score;
    g->total+=score;
    g->phase=GUESS_RESULT;return true;
}
bool guess_advance(GuessGame *g){
    if(!g||g->phase!=GUESS_RESULT)return false;
    g->round++;
    g->attempt=0;
    g->has_pin=false;
    g->last_tick=0;
    g->phase=g->round>=GUESS_ROUNDS?GUESS_COMPLETE:GUESS_FINDING;
    return true;
}
void guess_tick(GuessGame *g,uint64_t now_ms){
    if(!g||g->phase!=GUESS_PLAYING)return;
    if(!g->last_tick){g->last_tick=now_ms;return;}
    uint64_t elapsed=now_ms>g->last_tick?now_ms-g->last_tick:0;
    g->last_tick=now_ms;
    if(!g->time_limit_ms)return;
    /* If the console was asleep, don't count the entire lid-closed period. */
    if(elapsed>1000)elapsed=0;
    if(elapsed>=g->remaining_ms){g->remaining_ms=0;guess_submit(g);}
    else g->remaining_ms-=(uint32_t)elapsed;
}
void guess_toggle_pause(GuessGame *g,uint64_t now_ms){
    if(!g)return;
    if(g->phase==GUESS_PAUSED){g->phase=g->resume;g->last_tick=now_ms;}
    else if(g->phase==GUESS_PLAYING||g->phase==GUESS_FINDING){g->resume=g->phase;g->phase=GUESS_PAUSED;}
}
void guess_error(GuessGame *g){if(g)g->phase=GUESS_ERROR;}
/* Web Mercator geography: independent from the normal map camera. */
static double merc_y(double lat){
    double phi=clamp_lat(lat)*DEG;
    return (1.0-asinh(tan(phi))/PI)/2.0;
}
static double inv_merc(double y){
    return atan(sinh(PI*(1.0-2.0*y)))/DEG;
}
void guess_map_center_delta(GuessPoint *p,int zoom,double dx,double dy){
    if(!p||zoom<0||zoom>18)return;
    double world=(double)(256u<<zoom);
    p->lat=clamp_lat(inv_merc(fmax(0.0,fmin(1.0,merc_y(p->lat)+dy/world))));
    p->lon=wrap_lon(p->lon+dx/world*360.0);
}
GuessPoint guess_map_at_pixel(GuessPoint center,int zoom,double px,double py,
                              double width,double height){
    guess_map_center_delta(&center,zoom,px-width/2.0,py-height/2.0);
    return center;
}
void guess_map_project(GuessPoint center,GuessPoint point,int zoom,
                       double width,double height,float *x,float *y){
    if(!x||!y||zoom<0||zoom>18)return;
    double world=(double)(256u<<zoom);
    *x=(float)(width/2.0+wrap_lon(point.lon-center.lon)/360.0*world);
    *y=(float)(height/2.0+(merc_y(point.lat)-merc_y(center.lat))*world);
}