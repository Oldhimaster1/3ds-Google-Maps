#include "guess_roads.h"
#include <math.h>
#include <string.h>
#define RECORD_BYTES 20u
#define HEADER_BYTES 16u
#define MAX_RECORDS 1000000u
static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|
           (uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int32_t lei32(const unsigned char *p) {return (int32_t)le32(p);}
static uint64_t next64(uint64_t *r) {
    uint64_t z=(*r+=UINT64_C(0x9e3779b97f4a7c15));
    z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
    z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);
    return z^(z>>31);
}
static double unit(uint64_t *s) {
    return (double)(next64(s)>>11)*(1.0/9007199254740992.0);
}
static double wrap(double lon) {
    lon=fmod(lon+180.0,360.0);
    if(lon<0)lon+=360.0;
    return lon-180.0;
}
void guess_roads_close(GuessRoads *r) {
    if(!r)return;
    if(r->file)fclose(r->file);
    r->file=NULL;
    r->records=0;
}
bool guess_roads_open(GuessRoads *r,const char *path) {
    if(!r||!path)return false;
    guess_roads_close(r);
    FILE *f=fopen(path,"rb");
    if(!f)return false;
    unsigned char header[HEADER_BYTES];
    bool ok=fread(header,1,sizeof(header),f)==sizeof(header) &&
            memcmp(header,"SGROAD1\0",8)==0;
    if(ok){
        uint32_t count=le32(header+8),recbytes=le32(header+12);
        ok=count>0&&count<=MAX_RECORDS&&recbytes==RECORD_BYTES;
        if(ok){
            /* Check length rather than trusting a corrupt count. */
            if(fseek(f,0,SEEK_END)!=0)ok=false;
            else {
                long size=ftell(f);
                ok=size>=0 && (uint64_t)size==
                   HEADER_BYTES+(uint64_t)count*RECORD_BYTES;
            }
            if(ok){
                r->file=f;
                r->records=count;
                return true;
            }
        }
    }
    fclose(f);
    return false;
}
bool guess_roads_sample(GuessRoads *r,uint64_t *rng,GuessRegion region,
                        GuessPoint *out){
    if(!r||!r->file||!r->records||!rng||!out||
       region<GUESS_WORLD||region>=GUESS_MAP_COUNT)return false;
    unsigned flag=region==GUESS_USA?2u:region==GUESS_EUROPE?4u:1u;
    for(unsigned attempt=0;attempt<128;attempt++){
        uint32_t idx=(uint32_t)(next64(rng)%r->records);
        long position=(long)(HEADER_BYTES+(uint64_t)idx*RECORD_BYTES);
        unsigned char b[RECORD_BYTES];
        if(fseek(r->file,position,SEEK_SET)!=0 ||
           fread(b,1,sizeof(b),r->file)!=sizeof(b))return false;
        uint32_t mask=le32(b+16);
        if(!(mask&flag))continue;
        double lat0=lei32(b+0)*.000001,lon0=lei32(b+4)*.000001;
        double lat1=lei32(b+8)*.000001,lon1=lei32(b+12)*.000001;
        if(fabs(lat0)>85.05112878||fabs(lat1)>85.05112878||
           fabs(lon0)>180||fabs(lon1)>180||
           (lat0==lat1&&lon0==lon1))continue;
        double delta=lon1-lon0;
        if(delta>180)delta-=360;
        if(delta< -180)delta+=360;
        double t=unit(rng);
        out->lat=lat0+(lat1-lat0)*t;
        out->lon=wrap(lon0+delta*t);
        return true;
    }
    return false;
}
