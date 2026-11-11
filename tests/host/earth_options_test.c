#include "earth_options.h"
#include <assert.h>
#include <stdio.h>
int main(void){EarthOptions x;earth_options_default(&x);
 assert(x.input_speed==1&&x.panorama_quality==3&&x.buildings==0);
 x.input_speed=2;x.terrain_exaggeration=3;x.panorama_quality=2;x.buildings=1;
 const char *path="/tmp/earth-options-test.cfg";
 assert(earth_options_save(&x,path));earth_options_default(&x);
 assert(earth_options_load(&x,path));assert(x.input_speed==2&&x.terrain_exaggeration==3&&x.buildings==1);
 assert(earth_options_terrain_scale(&x)==3&&earth_options_stick_scale(&x)>1.5);
 remove(path);
 x.dead_zone=999;earth_options_clamp(&x);assert(x.dead_zone==60);
 assert(!earth_options_load(&x,path));
 puts("Expanded persistent controls and graphics settings passed");return 0;
}
