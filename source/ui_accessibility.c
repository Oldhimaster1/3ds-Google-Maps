#include "ui_accessibility.h"
#include <stdio.h>
#include <string.h>
static UiAccessConfig settings={1,1,true};
const UiAccessConfig *ui_access_get(void){return &settings;}
void ui_access_set(unsigned size,unsigned weight,bool high_contrast){
    settings.size=size>2?2:size;
    settings.weight=weight>2?2:weight;
    settings.high_contrast=high_contrast;
}
void ui_access_reset(void){settings=(UiAccessConfig){1,1,true};}
float ui_access_scale(void){
    static const float scale[3]={1.0f,1.13f,1.27f};
    return scale[settings.size];
}
bool ui_access_load(const char *path){
    if(!path || !*path)return false;
    FILE *fp=fopen(path,"rb");if(!fp)return false;
    unsigned version=0,size=0,weight=0,contrast=0;
    int matched=fscanf(fp,"%u %u %u %u",&version,&size,&weight,&contrast);
    fclose(fp);
    if(matched!=4 || version!=1 || size>2 || weight>2 || contrast>1)return false;
    ui_access_set(size,weight,contrast!=0);return true;
}
bool ui_access_save(const char *path){
    if(!path || !*path)return false;
    char tmp[320];int n=snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    if(n<=0 || n>=(int)sizeof(tmp))return false;
    FILE *fp=fopen(tmp,"wb");if(!fp)return false;
    int ok=fprintf(fp,"1 %u %u %u\n",settings.size,settings.weight,
                   settings.high_contrast?1u:0u)>0;
    if(fclose(fp)!=0)ok=0;
    if(!ok || rename(tmp,path)!=0){remove(tmp);return false;}
    return true;
}