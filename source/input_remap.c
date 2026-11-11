#include "input_remap.h"
#include <3ds.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
/* Keep START as an untouchable hardware escape; touch and analog are unmodified.
 * Within each context, the 11 mapped digital keys form a bijection. */
static const InputRemap *active_map;
void input_remap_set_active(const InputRemap *m){active_map=m;}
uint32_t input_remap_apply(unsigned profile,uint32_t raw){
 return input_remap_keys(active_map,profile,raw);
}
static const uint32_t keys[INPUT_REMAP_BUTTONS]={
 KEY_A,KEY_B,KEY_X,KEY_Y,KEY_L,KEY_R,
 KEY_DUP,KEY_DDOWN,KEY_DLEFT,KEY_DRIGHT,KEY_SELECT
};
static const char *names[INPUT_REMAP_BUTTONS]={
 "A","B","X","Y","L","R","D-Up","D-Down","D-Left","D-Right","Select"
};
static const char *contexts[INPUT_REMAP_PROFILES]={
 "Map / menus","Street View","Globe / terrain","StreetGuess"
};
static uint32_t mask_all(void){
 uint32_t mask=0;
 for(unsigned i=0;i<INPUT_REMAP_BUTTONS;i++)mask|=keys[i];
 return mask;
}
void input_remap_default(InputRemap *m){
 if(!m)return;
 for(unsigned p=0;p<INPUT_REMAP_PROFILES;p++)
  for(unsigned i=0;i<INPUT_REMAP_BUTTONS;i++)m->physical[p][i]=(uint8_t)i;
}
bool input_remap_valid(const InputRemap *m){
 if(!m)return false;
 for(unsigned p=0;p<INPUT_REMAP_PROFILES;p++){
  unsigned seen=0;
  for(unsigned i=0;i<INPUT_REMAP_BUTTONS;i++){
   unsigned physical=m->physical[p][i];
   if(physical>=INPUT_REMAP_BUTTONS || (seen&(1u<<physical)))return false;
   seen|=1u<<physical;
  }
 }
 return true;
}
bool input_remap_bind(InputRemap *m,unsigned profile,unsigned action,unsigned physical){
 if(!m||!input_remap_valid(m)||profile>=INPUT_REMAP_PROFILES||
    action>=INPUT_REMAP_BUTTONS||physical>=INPUT_REMAP_BUTTONS)return false;
 unsigned other=0;
 while(other<INPUT_REMAP_BUTTONS && m->physical[profile][other]!=physical)other++;
 if(other>=INPUT_REMAP_BUTTONS)return false;
 uint8_t old=m->physical[profile][action];
 m->physical[profile][action]=(uint8_t)physical;
 m->physical[profile][other]=old;
 return true;
}
uint32_t input_remap_keys(const InputRemap *m,unsigned profile,uint32_t raw){
 if(!m||profile>=INPUT_REMAP_PROFILES||!input_remap_valid(m))return raw;
 uint32_t result=raw & ~mask_all();
 for(unsigned i=0;i<INPUT_REMAP_BUTTONS;i++)
  if(raw&keys[m->physical[profile][i]])result|=keys[i];
 return result;
}
int input_remap_pressed(uint32_t raw_down){
 int found=-1;
 for(unsigned i=0;i<INPUT_REMAP_BUTTONS;i++)
  if(raw_down&keys[i]){if(found>=0)return -1;found=(int)i;}
 return found;
}
const char *input_remap_button_name(unsigned i){
 return i<INPUT_REMAP_BUTTONS?names[i]:"?";
}
const char *input_remap_profile_name(unsigned p){
 return p<INPUT_REMAP_PROFILES?contexts[p]:"?";
}
bool input_remap_load(InputRemap *m,const char *path){
 if(!m||!path)return false;
 FILE *f=fopen(path,"rb");if(!f)return false;
 InputRemap next;input_remap_default(&next);
 unsigned version=0;
 bool ok=fscanf(f,"%u",&version)==1 && version==1;
 for(unsigned p=0;p<INPUT_REMAP_PROFILES;p++)
  for(unsigned i=0;i<INPUT_REMAP_BUTTONS;i++){
   unsigned v=0;
   if(!ok||fscanf(f,"%u",&v)!=1||v>=INPUT_REMAP_BUTTONS){ok=false;break;}
   next.physical[p][i]=(uint8_t)v;
  }
 int extra;
 while((extra=fgetc(f))!=EOF)if(!isspace((unsigned char)extra))ok=false;
 if(ferror(f))ok=false;
 fclose(f);
 if(!ok||!input_remap_valid(&next))return false;
 *m=next;return true;
}
bool input_remap_save(const InputRemap *m,const char *path){
 if(!input_remap_valid(m)||!path)return false;
 char temp[320];
 int n=snprintf(temp,sizeof(temp),"%s.tmp",path);
 if(n<=0||(size_t)n>=sizeof(temp))return false;
 FILE *f=fopen(temp,"wb");if(!f)return false;
 bool ok=fprintf(f,"1\n")>0;
 for(unsigned p=0;p<INPUT_REMAP_PROFILES;p++){
  for(unsigned i=0;i<INPUT_REMAP_BUTTONS;i++)
   if(fprintf(f,"%u%c",(unsigned)m->physical[p][i],
              i+1==INPUT_REMAP_BUTTONS?'\n':' ')<=0)ok=false;
 }
 if(fclose(f)!=0)ok=false;
 if(!ok||rename(temp,path)!=0){remove(temp);return false;}
 return true;
}

void input_remap_hint(unsigned profile,const char *text,char *out,size_t capacity){
 if(!out||capacity==0)return;
 size_t used=0;
 if(!text){out[0]=0;return;}
 while(*text&&used+1<capacity){
  const char *replacement=NULL;size_t consumed=1;
  bool left=text[0] && (used==0 || text[-1]==' ' || text[-1]=='/' || text[-1]=='+' || text[-1]=='[');
  if(left && active_map && profile<INPUT_REMAP_PROFILES && input_remap_valid(active_map)){
   for(unsigned i=0;i<INPUT_REMAP_BUTTONS;i++){
    size_t n=strlen(names[i]);
    if(!strncmp(text,names[i],n) && (!text[n] || text[n]==' ' || text[n]=='/' || text[n]=='+' || text[n]==':' || text[n]==']')){
     replacement=names[active_map->physical[profile][i]];consumed=n;break;
    }
   }
  }
  if(replacement){size_t n=strlen(replacement);if(n>=capacity-used)break;memcpy(out+used,replacement,n);used+=n;}
  else out[used++]=*text;
  text+=consumed;
 }
 out[used]=0;
}
