#include "intl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void){
    char visual[INTL_TEXT_CAP];
    assert(intl_visual("Paris 123",visual,sizeof(visual)));
    assert(!strcmp(visual,"Paris 123"));
    assert(intl_visual("سلام",visual,sizeof(visual)));
    assert(!strcmp(visual,"ﻡﻼﺳ")); /* meem, lam-alef ligature, initial seen */
    assert(intl_visual("شارع 123",visual,sizeof(visual)));
    assert(strstr(visual,"123") && !strstr(visual,"321"));
    assert(intl_visual("שלום",visual,sizeof(visual)) && !strcmp(visual,"םולש"));
    assert(intl_visual("سلام\nParis",visual,sizeof(visual)));
    assert(!strcmp(visual,"ﻡﻼﺳ\nParis"));
    assert(intl_visual("Москва / İstanbul / Αθήνα",visual,sizeof(visual)));
    assert(!strcmp(visual,"Москва / İstanbul / Αθήνα"));
    assert(!intl_visual("hello",visual,3) && visual[0]==0);
    char oversized[1025];memset(oversized,'a',1024);oversized[1024]=0;
    assert(!intl_visual(oversized,visual,sizeof(visual)));
    for(unsigned lang=0;lang<INTL_LANGUAGE_COUNT;lang++){
        intl_set_language(lang);
        assert(intl_visual(intl_language_name(lang),visual,sizeof(visual)));
        assert(*visual);
        assert(intl_visual(intl_translate("Settings"),visual,sizeof(visual)));
        if(lang)assert(strcmp(intl_translate("Settings"),"Settings"));
    }
    intl_set_language(0);
    char tiny[2];assert(intl_snprintf(tiny,sizeof(tiny),"%s","é")==2 && tiny[0]==0);
    char exact[3];assert(intl_snprintf(exact,sizeof(exact),"%s","é")==2 && !strcmp(exact,"é"));
    intl_set_language(1);
    char display[INTL_TEXT_CAP];
    assert(intl_display("12 buildings loaded",display,sizeof(display)));
    assert(strstr(display,"12") && strcmp(display,"12 buildings loaded"));
    assert(intl_display("Panorama detail z4 loaded",display,sizeof(display)));
    assert(strstr(display,"4") && strcmp(display,"Panorama detail z4 loaded"));
    assert(intl_display("Unmatched private place name",display,sizeof(display)) && !strcmp(display,"Unmatched private place name"));
    assert(!intl_display("12 buildings loaded",display,2));
    intl_set_language(2);
    assert(intl_display("12 buildings loaded",display,sizeof(display)) && strstr(display,"12"));
    intl_set_language(1);intl_set_music(true);
    intl_set_music_volume(25);assert(intl_save("language.cfg"));intl_set_language(0);intl_set_music(false);
    assert(intl_load("language.cfg") && intl_language()==1 && intl_music() && intl_music_volume()==25);
    FILE *f=fopen("language.cfg","wb");assert(f);fputs("1 999 2\n",f);fclose(f);
    assert(!intl_load("language.cfg") && intl_language()==1 && intl_music());
    f=fopen("language.cfg","wb");assert(f);fputs("1 1 1\n",f);fclose(f);
    assert(intl_load("language.cfg") && intl_music_volume()==55);
    f=fopen("language.cfg","wb");assert(f);fputs("2 0 1 101\n",f);fclose(f);
    assert(!intl_load("language.cfg") && intl_music_volume()==55);
    unlink("language.cfg");
    puts("PASS Arabic joining/ligatures, bidi numbers, Hebrew, UTF-8, nine locales and preference persistence");
}
