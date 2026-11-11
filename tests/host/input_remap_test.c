#include "input_remap.h"
#include <3ds.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 InputRemap m;input_remap_default(&m);assert(input_remap_valid(&m));
 assert(input_remap_keys(&m,REMAP_MAP,KEY_A|KEY_START|KEY_TOUCH)==(KEY_A|KEY_START|KEY_TOUCH));
 assert(input_remap_bind(&m,REMAP_MAP,0,1));
 assert(input_remap_keys(&m,REMAP_MAP,KEY_B)==KEY_A);
 assert(input_remap_keys(&m,REMAP_MAP,KEY_A)==KEY_B);
 assert(input_remap_keys(&m,REMAP_STREET,KEY_A)==KEY_A);
 assert(input_remap_keys(&m,REMAP_MAP,KEY_START|KEY_TOUCH)==(KEY_START|KEY_TOUCH));
 assert(input_remap_pressed(KEY_A)==0 && input_remap_pressed(KEY_B)==1);
 assert(input_remap_pressed(KEY_A|KEY_B)==-1);
 assert(!input_remap_bind(&m,REMAP_MAP,0,INPUT_REMAP_BUTTONS));
 assert(!input_remap_bind(&m,REMAP_MAP,INPUT_REMAP_BUTTONS,0));
 for(unsigned p=0;p<INPUT_REMAP_PROFILES;p++)
  for(unsigned a=0;a<INPUT_REMAP_BUTTONS;a++)
   for(unsigned b=0;b<INPUT_REMAP_BUTTONS;b++){
    assert(input_remap_bind(&m,p,a,b));
    assert(input_remap_valid(&m));
   }
 const char *path="/tmp/3ds-input-remap-test.cfg";
 assert(input_remap_save(&m,path));
 InputRemap loaded;input_remap_default(&loaded);
 assert(input_remap_load(&loaded,path));
 assert(memcmp(&m,&loaded,sizeof(m))==0);
 FILE *tail=fopen(path,"ab");assert(tail);fputs("corrupt trailing bytes",tail);fclose(tail);
 assert(!input_remap_load(&loaded,path));assert(memcmp(&m,&loaded,sizeof(m))==0);
 FILE *f=fopen(path,"wb");assert(f);
 fputs("1\n0 0 0 0 0 0 0 0 0 0 0\n",f);fclose(f);
 assert(!input_remap_load(&loaded,path));
 assert(memcmp(&m,&loaded,sizeof(m))==0);
 remove(path);
 assert(!input_remap_save(&m,"/this/does/not/exist/path.cfg"));
 input_remap_default(&m);input_remap_bind(&m,REMAP_STREET,0,1);input_remap_set_active(&m);
 char hint[80];input_remap_hint(REMAP_STREET,"A Map B Back L/R Move",hint,sizeof(hint));
 assert(!strcmp(hint,"B Map A Back L/R Move"));
 input_remap_hint(REMAP_MAP,"A Map B Back",hint,sizeof(hint));assert(!strcmp(hint,"A Map B Back"));
 input_remap_hint(REMAP_STREET,"A Map",hint,2);assert(hint[1]==0);
 input_remap_bind(&m,REMAP_STREET,10,4);
 input_remap_bind(&m,REMAP_STREET,6,2);
 input_remap_hint(REMAP_STREET,"SELECT: search (A) Up/Down",hint,sizeof(hint));
 assert(!strcmp(hint,"L: search (B) X/D-Down"));
 input_remap_hint(REMAP_STREET,"NASA GIBS / Blue Marble",hint,sizeof(hint));
 assert(!strcmp(hint,"NASA GIBS / Blue Marble"));
 input_remap_set_active(NULL);
 puts("input remap: PASS");
 return 0;
}
