#include "ui_accessibility.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 const char *path="/tmp/3ds-access-ui-test.cfg";remove(path);
 ui_access_reset();
 assert(ui_access_get()->size==1 && ui_access_get()->weight==1 && ui_access_get()->high_contrast);
 assert(!ui_access_load(path));
 ui_access_set(2,2,false);
 assert(ui_access_scale()>1.26f && ui_access_scale()<1.28f);
 assert(ui_access_save(path));
 ui_access_set(0,0,true);
 assert(ui_access_load(path));
 assert(ui_access_get()->size==2 && ui_access_get()->weight==2 && !ui_access_get()->high_contrast);
 ui_access_set(100,100,true);
 assert(ui_access_get()->size==2 && ui_access_get()->weight==2);
 FILE *f=fopen(path,"wb");assert(f);fputs("1 5 1 0\n",f);fclose(f);
 assert(!ui_access_load(path));
 assert(ui_access_get()->size==2);
 remove(path);
 puts("UI accessibility preferences passed");return 0;
}