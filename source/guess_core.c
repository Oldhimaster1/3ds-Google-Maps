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
/* Population-aware sampling priors, NOT panorama locations. Each point is a
 * regional anchor and is jittered continuously within many square kilometres.
 * Google's live response, not this table, determines playable panoramas. */
typedef struct { float lat, lon; unsigned scope; } Hub;
static const Hub hubs[]={
 {40.71f,-74.01f,W|U},{34.05f,-118.24f,W|U},{41.88f,-87.63f,W|U},
 {29.76f,-95.37f,W|U},{32.77f,-96.79f,W|U},{33.45f,-112.07f,W|U},
 {39.74f,-104.99f,W|U},{47.61f,-122.33f,W|U},{37.77f,-122.42f,W|U},
 {38.90f,-77.04f,W|U},{25.76f,-80.19f,W|U},{28.54f,-81.38f,W|U},
 {33.75f,-84.39f,W|U},{36.17f,-115.14f,W|U},{45.52f,-122.68f,W|U},
 {42.36f,-71.06f,W|U},{39.95f,-75.17f,W|U},{35.23f,-80.84f,W|U},
 {36.16f,-86.78f,W|U},{44.98f,-93.27f,W|U},{39.10f,-94.58f,W|U},
 {30.27f,-97.74f,W|U},{32.72f,-117.16f,W|U},
 {51.51f,-0.13f,W|E},{53.48f,-2.24f,W|E},{55.95f,-3.19f,W|E},
 {48.86f,2.35f,W|E},{45.76f,4.84f,W|E},{43.30f,5.37f,W|E},
 {52.52f,13.40f,W|E},{48.14f,11.58f,W|E},{50.11f,8.68f,W|E},
 {53.55f,10.00f,W|E},{50.94f,6.96f,W|E},{41.39f,2.17f,W|E},
 {40.42f,-3.70f,W|E},{39.47f,-0.38f,W|E},{41.90f,12.50f,W|E},
 {45.46f,9.19f,W|E},{40.85f,14.27f,W|E},{52.37f,4.90f,W|E},
 {51.22f,4.40f,W|E},{50.85f,4.35f,W|E},{47.50f,19.04f,W|E},
 {50.08f,14.44f,W|E},{48.21f,16.37f,W|E},{52.23f,21.01f,W|E},
 {50.06f,19.94f,W|E},{59.33f,18.06f,W|E},{55.68f,12.57f,W|E},
 {59.91f,10.75f,W|E},{60.17f,24.94f,W|E},{38.72f,-9.14f,W|E},
 {41.15f,-8.61f,W|E},{44.43f,26.10f,W|E},{42.70f,23.32f,W|E},
 {37.98f,23.73f,W|E},{41.01f,28.98f,W|E},{47.38f,8.54f,W|E},
 {46.06f,14.51f,W|E},{45.81f,15.98f,W|E},
 {43.65f,-79.38f,W},{45.50f,-73.57f,W},{49.28f,-123.12f,W},
 {19.43f,-99.13f,W},{20.67f,-103.35f,W},{25.69f,-100.31f,W},
 {-23.55f,-46.63f,W},{-22.91f,-43.20f,W},{-19.92f,-43.94f,W},
 {-34.60f,-58.38f,W},{-31.42f,-64.19f,W},{-33.45f,-70.67f,W},
 {-12.05f,-77.04f,W},{4.71f,-74.07f,W},{6.24f,-75.58f,W},
 {19.07f,72.88f,W},{28.61f,77.21f,W},{12.97f,77.59f,W},
 {22.57f,88.36f,W},{23.02f,72.57f,W},{13.08f,80.27f,W},
 {31.23f,121.47f,W},{39.90f,116.40f,W},{23.13f,113.26f,W},
 {22.54f,114.06f,W},{30.57f,104.07f,W},{29.56f,106.55f,W},
 {35.68f,139.69f,W},{34.69f,135.50f,W},{35.18f,136.91f,W},
 {37.57f,126.98f,W},{35.18f,129.08f,W},{25.04f,121.57f,W},
 {13.75f,100.50f,W},{18.79f,98.99f,W},{10.82f,106.63f,W},
 {21.03f,105.85f,W},{1.35f,103.82f,W},{3.14f,101.69f,W},
 {-6.21f,106.85f,W},{-7.26f,112.75f,W},{14.60f,120.98f,W},
 {-33.87f,151.21f,W},{-37.81f,144.96f,W},{-27.47f,153.03f,W},
 {-31.95f,115.86f,W},{-36.85f,174.76f,W},{-41.29f,174.78f,W},
 {-26.20f,28.05f,W},{-33.92f,18.42f,W},{-29.86f,31.02f,W},
 {-1.29f,36.82f,W},{-4.05f,39.67f,W},{9.03f,38.75f,W},
 {6.52f,3.38f,W},{5.60f,-0.19f,W},{5.36f,-4.01f,W},
 {33.57f,-7.59f,W},{36.75f,3.06f,W},{30.04f,31.24f,W},
 {31.95f,35.93f,W},{32.09f,34.78f,W},{25.20f,55.27f,W},
 {24.71f,46.68f,W},{41.31f,69.28f,W},{43.24f,76.95f,W},
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
    /* Prefer density priors for a responsive first panorama, but keep a
     * substantial share of wide-area candidates for rural variety. */
    static const double urban_weights[4]={.80,.97,.32,.05};
    unsigned sampling=g->sampler<4?g->sampler:0;
    if(unit(&g->rng)<urban_weights[sampling]) {
        unsigned count=0;
        for(unsigned i=0;i<sizeof(hubs)/sizeof(hubs[0]);i++)
            if(hubs[i].scope&mask) count++;
        if(count){
            unsigned selected=(unsigned)(next64(&g->rng)%count);
            for(unsigned i=0;i<sizeof(hubs)/sizeof(hubs[0]);i++){
                if(!(hubs[i].scope&mask))continue;
                if(selected--!=0)continue;
                double angle=unit(&g->rng)*(2.0*PI);
                static const double radius_km[4]={55.0,18.0,130.0,160.0};
                double radius=sqrt(unit(&g->rng))*radius_km[sampling];
                out->lat=hubs[i].lat+radius*cos(angle)/111.2;
                out->lon=wrap_lon(hubs[i].lon+radius*sin(angle)/
                         (111.2*fmax(0.2,cos(out->lat*DEG))));
                return valid(*out);
            }
        }
    }
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