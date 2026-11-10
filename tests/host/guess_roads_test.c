#include "guess_roads.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
static void put32(unsigned char *p,uint32_t v){
 p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);
 p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);
}
static void record(FILE *f,int32_t lat0,int32_t lon0,int32_t lat1,int32_t lon1,uint32_t mask){
 unsigned char b[20]={0};put32(b,(uint32_t)lat0);put32(b+4,(uint32_t)lon0);
 put32(b+8,(uint32_t)lat1);put32(b+12,(uint32_t)lon1);put32(b+16,mask);
 assert(fwrite(b,1,20,f)==20);
}
int main(void){
 const char *p="/tmp/guess-roads-test.bin";
 FILE *f=fopen(p,"wb");assert(f);
 unsigned char h[16]={0};memcpy(h,"SGROAD1\0",8);put32(h+8,3);put32(h+12,20);
 assert(fwrite(h,1,16,f)==16);
 record(f,40000000,-74000000,40005000,-73995000,1|2);
 record(f,48000000,2000000,48004000,2005000,1|4);
 record(f,12000000,179999000,12000000,-179999000,1);
 fclose(f);
 GuessRoads r={0};assert(guess_roads_open(&r,p)&&r.records==3);
 uint64_t rng=0x1234;GuessPoint q;
 for(int i=0;i<500;i++){
  assert(guess_roads_sample(&r,&rng,GUESS_USA,&q));
  assert(q.lat>=40.0&&q.lat<=40.005&&q.lon>=-74&&q.lon<=-73.995);
  assert(guess_roads_sample(&r,&rng,GUESS_EUROPE,&q));
  assert(q.lat>=48&&q.lat<=48.004&&q.lon>=2&&q.lon<=2.005);
  assert(guess_roads_sample(&r,&rng,GUESS_WORLD,&q));
  assert(isfinite(q.lat)&&isfinite(q.lon)&&fabs(q.lon)<=180);
 }
 guess_roads_close(&r);
 /* Reject a corrupted/truncated pack before serving anything. */
 f=fopen(p,"rb+");assert(f);assert(fseek(f,0,SEEK_SET)==0);h[12]=19;
 assert(fseek(f,12,SEEK_SET)==0);assert(fwrite(h+12,1,1,f)==1);fclose(f);
 assert(!guess_roads_open(&r,p));
 remove(p);
 puts("StreetGuess optional road-pack random-access/region/corruption tests passed");
 return 0;
}