#include "earth_options.h"
#include <stdio.h>
#include <string.h>
static unsigned clip(unsigned v,unsigned hi){return v>hi?hi:v;}
void earth_options_default(EarthOptions *p){if(p)*p=(EarthOptions){1,20,3,0,0,0,1,1};}
void earth_options_clamp(EarthOptions *p){if(!p)return;
 p->input_speed=clip(p->input_speed,2);
 if(p->dead_zone<10)p->dead_zone=10;
 if(p->dead_zone>60)p->dead_zone=60;
 p->panorama_quality=clip(p->panorama_quality,3);
 p->sampler=clip(p->sampler,3);
 p->terrain_exaggeration=clip(p->terrain_exaggeration,4);
 p->buildings=clip(p->buildings,1);
 p->globe_detail=clip(p->globe_detail,2);
 p->gyro_invert=clip(p->gyro_invert,1);
}
float earth_options_terrain_scale(const EarthOptions *p){const float a[5]={1,1.5f,2,3,4};return p?a[clip(p->terrain_exaggeration,4)]:1;}
float earth_options_stick_scale(const EarthOptions *p){const float a[3]={.6f,1.f,1.6f};return p?a[clip(p->input_speed,2)]:1;}
bool earth_options_load(EarthOptions *p,const char *file){if(!p||!file)return false;
 FILE *f=fopen(file,"rb");if(!f)return false;
 unsigned version;EarthOptions next;
 int n=fscanf(f,"%u %u %u %u %u %u %u %u %u",&version,
  &next.input_speed,&next.dead_zone,&next.panorama_quality,&next.sampler,
  &next.terrain_exaggeration,&next.buildings,&next.globe_detail,&next.gyro_invert);
 fclose(f);
 if(n!=9||version!=2)return false;
 earth_options_clamp(&next);*p=next;return true;
}
bool earth_options_save(const EarthOptions *p,const char *file){if(!p||!file)return false;
 char temp[320];int n=snprintf(temp,sizeof(temp),"%s.tmp",file);
 if(n<=0||n>=(int)sizeof(temp))return false;
 EarthOptions q=*p;earth_options_clamp(&q);
 FILE *f=fopen(temp,"wb");if(!f)return false;
 int ok=fprintf(f,"2 %u %u %u %u %u %u %u %u\n",q.input_speed,q.dead_zone,
  q.panorama_quality,q.sampler,q.terrain_exaggeration,q.buildings,
  q.globe_detail,q.gyro_invert)>0;
 if(fclose(f)!=0)ok=0;
 if(!ok||rename(temp,file)!=0){remove(temp);return false;}return true;
}
