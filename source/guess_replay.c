#include "guess_replay.h"
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool cell(double lat,double lon,GuessReplayCell *out){
 if(!out||!isfinite(lat)||!isfinite(lon)||fabs(lat)>85.05112878||fabs(lon)>180)return false;
 if(lon==180)lon=-180;
 /* One quarter of a degree per cell; this is based on independently generated
    geographic positions and doesn't index any imagery or panorama IDs. */
 out->lat_q=(int16_t)floor((lat+90.)*4);
 out->lon_q=(int16_t)floor((lon+180.)*4);
 return true;
}
void guess_replay_clear(GuessReplay *h){if(h)memset(h,0,sizeof(*h));}
bool guess_replay_can_play(const GuessReplay *h,double lat,double lon,unsigned radius){
 GuessReplayCell p;if(!cell(lat,lon,&p))return false;
 if(!h)return true;
 if(radius>5)radius=5;
 for(uint32_t i=0;i<h->count&&i<GUESS_REPLAY_MAX;i++){
  int dx=abs(h->cells[i].lon_q-p.lon_q);
  if(dx>720)dx=1440-dx;
  int dy=abs(h->cells[i].lat_q-p.lat_q);
  if(dx<=(int)radius&&dy<=(int)radius)return false;
 }
 return true;
}
void guess_replay_add(GuessReplay *h,double lat,double lon){
 if(!h)return;
 GuessReplayCell p;if(!cell(lat,lon,&p))return;
 h->cells[h->cursor]=p;
 h->cursor=(h->cursor+1)%GUESS_REPLAY_MAX;
 if(h->count<GUESS_REPLAY_MAX)h->count++;
}
bool guess_replay_save(const GuessReplay *h,const char *path){
 if(!h||!path||h->count>GUESS_REPLAY_MAX||h->cursor>=GUESS_REPLAY_MAX)return false;
 char temp[320];int n=snprintf(temp,sizeof(temp),"%s.tmp",path);
 if(n<=0||n>=(int)sizeof(temp))return false;
 FILE *f=fopen(temp,"wb");if(!f)return false;
 int ok=fprintf(f,"SGH2 %" PRIu32 " %" PRIu32 "\n",h->count,h->cursor)>0;
 for(uint32_t i=0;i<h->count;i++)
  if(fprintf(f,"%d %d\n",h->cells[i].lat_q,h->cells[i].lon_q)<=0)ok=0;
 if(fclose(f)!=0)ok=0;
 if(!ok||rename(temp,path)!=0){remove(temp);return false;}return true;
}
bool guess_replay_load(GuessReplay *h,const char *path){
 if(!h||!path)return false;
 FILE *f=fopen(path,"rb");if(!f)return false;
 GuessReplay t={0};char magic[6]={0};
 int ok=fscanf(f,"%5s %" SCNu32 " %" SCNu32,magic,&t.count,&t.cursor)==3&&
        strcmp(magic,"SGH2")==0&&t.count<=GUESS_REPLAY_MAX&&t.cursor<GUESS_REPLAY_MAX;
 for(uint32_t i=0;ok&&i<t.count;i++){
  int a,b;ok=fscanf(f,"%d %d",&a,&b)==2 && a>=0 && a<720 && b>=0 && b<1440;
  if(ok)t.cells[i]=(GuessReplayCell){(int16_t)a,(int16_t)b};
 }
 if(fclose(f)!=0)ok=0;
 if(ok)*h=t;
 return ok;
}
