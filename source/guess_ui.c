#include "guess_ui.h"
#include "guess_core.h"
#include "pano_runtime.h"
#include "explore.h"
#include "map_tiles.h"
#include "pano_gyro.h"
#include "street_look_input.h"
#include <citro2d.h>
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define PI 3.14159265358979323846f
#define GUESS_SAVES "sdmc:/3ds_google_maps/streetguess_records.txt"
#define GUESS_MAP_HEIGHT 194

typedef struct {
    bool active, scene_loaded, fetching, touch_active, touch_dragged, gyro;
    int map_zoom, time_choice, last_touch_x, last_touch_y, start_touch_x, start_touch_y;
    float yaw,pitch,fov,gyro_coeff;
    PanoGyroFilter gyro_filter;
    u64 last_ms, next_search_ms;
    GuessPoint candidate, map_center;
    char start_id[48], note[96];
    GuessGame game;
    unsigned best[GUESS_MAP_COUNT][GUESS_RULE_COUNT], games;
    C2D_TextBuf text;
    bool record_screen;
} GuessUi;
static GuessUi ui;
static const unsigned times[]={0,30,60,120,180,300};
static const char *region_names[]={"World","United States","Europe"};
static const char *rule_names[]={"Moving","No Move","NMPZ"};
static void print(float x,float y,float scale,u32 color,const char *s) {
    if (!ui.text || !s || !*s)return;
    C2D_Text t;
    C2D_TextParse(&t,ui.text,s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t,C2D_WithColor,x,y,.85f,scale,scale,color);
}
static void card(float y,const char *heading,const char *detail){
    C2D_DrawRectSolid(10,y,.25f,300,35,C2D_Color32(25,40,58,255));
    C2D_DrawRectSolid(10,y,.26f,3,35,C2D_Color32(80,202,227,255));
    print(19,y+4,.45f,C2D_Color32(242,247,253,255),heading);
    if(detail)print(19,y+20,.31f,C2D_Color32(172,196,214,255),detail);
}
static void load_records(void){
    FILE *f=fopen(GUESS_SAVES,"r");
    if(!f)return;
    unsigned version=0,games=0,values[GUESS_MAP_COUNT][GUESS_RULE_COUNT]={0};
    bool ok=fscanf(f,"%u %u",&version,&games)==2&&version==1;
    for(int i=0;i<GUESS_MAP_COUNT;i++)for(int j=0;j<GUESS_RULE_COUNT;j++){
        if(ok && fscanf(f,"%u",&values[i][j])!=1)ok=false;
        if(values[i][j]>25000)ok=false;
    }
    fclose(f);
    if(ok){ui.games=games;memcpy(ui.best,values,sizeof(values));}
}
static void save_records(void){
    mkdir("sdmc:/3ds_google_maps",0777);
    FILE *f=fopen(GUESS_SAVES ".tmp","w");
    if(!f)return;
    int ok=fprintf(f,"1 %u\n",ui.games)>0;
    for(int i=0;i<GUESS_MAP_COUNT;i++)for(int j=0;j<GUESS_RULE_COUNT;j++)
        if(fprintf(f,"%u%c",ui.best[i][j],j==GUESS_RULE_COUNT-1?'\n':' ')<=0)ok=0;
    if(fclose(f)!=0)ok=0;
    if(ok)rename(GUESS_SAVES ".tmp",GUESS_SAVES);
    else remove(GUESS_SAVES ".tmp");
}
static void stop_gyro(void){
    if(ui.gyro) HIDUSER_DisableGyroscope();
    ui.gyro=false;
}
static void drop_scene(void){
    stop_gyro();
    pano_runtime_stop();
    if(ui.scene_loaded){explore_unload();ui.scene_loaded=false;}
    ui.fetching=false;
}
void guess_ui_leave(void){
    if(!ui.active)return;
    drop_scene();
    pano_runtime_end_game_google();
    if(ui.text){C2D_TextBufDelete(ui.text);ui.text=NULL;}
    ui.active=false;
    gfxSet3D(false);
    C2D_Prepare();
}
bool guess_ui_active(void){return ui.active;}
bool guess_ui_enter(void){
    if(ui.active)return true;
    memset(&ui,0,sizeof(ui));
    guess_init(&ui.game,(u64)svcGetSystemTick()^((u64)osGetTime()<<23));
    ui.time_choice=3;
    ui.fov=55;
    ui.text=C2D_TextBufNew(4096);
    if(!ui.text)return false;
    ui.active=true;
    ui.last_ms=osGetTime();
    load_records();
    gfxSet3D(false);
    C2D_Prepare();
    return true;
}
static void reset_guess_map(void){
    ui.map_zoom=ui.game.region==GUESS_WORLD?1:3;
    ui.map_center=ui.game.region==GUESS_WORLD?(GuessPoint){0,0}:
                  ui.game.region==GUESS_USA?(GuessPoint){39,-98}:(GuessPoint){51,11};
    ui.touch_active=false;ui.touch_dragged=false;
}
static void new_game(void){
    drop_scene();
    guess_begin(&ui.game,ui.game.region,ui.game.rules,times[ui.time_choice]);
    reset_guess_map();
    ui.yaw=0;ui.pitch=0;ui.fov=55;
    ui.start_id[0]=0;
    ui.next_search_ms=0;
    ui.note[0]=0;
    if(!explore_load(EXPLORE_STREET,0)){
        snprintf(ui.note,sizeof(ui.note),"Cannot create Street View renderer");
        guess_error(&ui.game);
        return;
    }
    ui.scene_loaded=true;
}
static void reset_round_scene(void){
    drop_scene();
    if(!explore_load(EXPLORE_STREET,0)){
        snprintf(ui.note,sizeof(ui.note),"Street View renderer is out of memory");
        guess_error(&ui.game);
        return;
    }
    ui.scene_loaded=true;
    ui.start_id[0]=0;
    ui.yaw=0;ui.pitch=0;ui.fov=55;
    reset_guess_map();
    ui.next_search_ms=0;
}
static void find_round(u64 now){
    if(!ui.scene_loaded||ui.game.phase!=GUESS_FINDING)return;
    if(!ui.fetching){
        if(now<ui.next_search_ms)return;
        if(!guess_random_candidate(&ui.game,&ui.candidate)){
            snprintf(ui.note,sizeof(ui.note),"No playable Google panorama found. Retry or exit.");
            guess_error(&ui.game);return;
        }
        /* Reject recently visited geographic cells BEFORE an HTTP lookup.
         * This does not store or index Google's imagery. */
        if(!guess_candidate_acceptable(&ui.game,ui.candidate)){
            ui.next_search_ms=now;
            return;
        }
        /* No repeated fixed panorama list. Independently randomized coordinates
         * are checked live with Google and accepted only after a real decode. */
        if(!pano_runtime_start_game_google(ui.candidate.lat,ui.candidate.lon)){
            snprintf(ui.note,sizeof(ui.note),"Wi-Fi/Google request unavailable");
            guess_error(&ui.game);return;
        }
        ui.fetching=true;
    }
    pano_runtime_poll();
    if(pano_runtime_has_live()){
        const PanoLiveImage *p=pano_runtime_current();
        GuessPoint actual={p->lat,p->lon};
        /* Avoid surprising cross-region images when a request snaps to a road
         * far from the sampled point. The true panorama position is scored. */
        bool good=p->provider==STREET_PROVIDER_GOOGLE&&p->is_pano&&
                  guess_distance_km(ui.candidate,actual)<40.0&&
                  guess_accept_start(&ui.game,actual,now);
        if(good){
            snprintf(ui.start_id,sizeof(ui.start_id),"%s",p->id);
            /* Deterministic per-round random initial direction, not always north. */
            ui.yaw=(float)(((ui.game.rng>>17)%360)*PI/180.0f);
            ui.fetching=false;
            ui.last_ms=now;
            return;
        }
        pano_runtime_stop();
        ui.fetching=false;
        ui.next_search_ms=now+150;
    }else if(!pano_runtime_network_active()){
        const char *reason=pano_runtime_status();
        if(strstr(reason,"rate limited")||strstr(reason,"denied access")){
            snprintf(ui.note,sizeof(ui.note),"Google declined requests; try again later");
            guess_error(&ui.game);
        }else{
            ui.fetching=false;
            ui.next_search_ms=now+180;
        }
    }
}
static void result_center(void){
    const GuessPoint a=ui.game.start,b=ui.game.pin;
    if(!ui.game.has_pin){ui.map_center=a;ui.map_zoom=3;return;}
    double dlon=fmod(b.lon-a.lon+540.0,360.0)-180.0;
    ui.map_center=(GuessPoint){(a.lat+b.lat)/2.0,a.lon+dlon/2.0};
    if(ui.map_center.lon>180)ui.map_center.lon-=360;
    if(ui.map_center.lon< -180)ui.map_center.lon+=360;
    ui.map_zoom=0;
    for(int z=0;z<=16;z++){
        float ax,ay,bx,by;
        guess_map_project(ui.map_center,a,z,320,GUESS_MAP_HEIGHT,&ax,&ay);
        guess_map_project(ui.map_center,b,z,320,GUESS_MAP_HEIGHT,&bx,&by);
        if(fabsf(ax-bx)>255||fabsf(ay-by)>140)break;
        ui.map_zoom=z;
    }
    if(ui.map_zoom>14)ui.map_zoom=14;
}
static void completed(void){
    ui.games++;
    unsigned *best=&ui.best[ui.game.region][ui.game.rules];
    if(ui.game.total>*best)*best=ui.game.total;
    save_records();
}
static void handle_menu(u32 keys,touchPosition touch){
    if(keys&KEY_B){guess_ui_leave();return;}
    if(keys&KEY_A){ui.record_screen=false;new_game();return;}
    if(!(keys&KEY_TOUCH))return;
    int y=touch.py;
    if(y>=40&&y<80){ui.record_screen=false;new_game();}
    else if(y>=83&&y<119)ui.game.region=(GuessRegion)((ui.game.region+1)%GUESS_MAP_COUNT);
    else if(y>=123&&y<159)ui.game.rules=(GuessRules)((ui.game.rules+1)%GUESS_RULE_COUNT);
    else if(y>=163&&y<198)ui.time_choice=(ui.time_choice+1)%6;
    else if(y>=203&&y<240)ui.record_screen=!ui.record_screen;
}
static void handle_map_touch(u32 keys,u32 held,touchPosition touch){
    if((keys&KEY_TOUCH)&&touch.py<GUESS_MAP_HEIGHT){
        ui.touch_active=true;ui.touch_dragged=false;
        ui.start_touch_x=ui.last_touch_x=touch.px;
        ui.start_touch_y=ui.last_touch_y=touch.py;
    }else if((held&KEY_TOUCH)&&ui.touch_active&&touch.py<GUESS_MAP_HEIGHT){
        int dx=touch.px-ui.last_touch_x,dy=touch.py-ui.last_touch_y;
        if(abs(touch.px-ui.start_touch_x)+abs(touch.py-ui.start_touch_y)>8)ui.touch_dragged=true;
        if(ui.touch_dragged)guess_map_center_delta(&ui.map_center,ui.map_zoom,-dx,-dy);
        ui.last_touch_x=touch.px;ui.last_touch_y=touch.py;
    }else if(!(held&KEY_TOUCH)&&ui.touch_active){
        if(!ui.touch_dragged&&ui.game.phase==GUESS_PLAYING)
            guess_set_pin(&ui.game,guess_map_at_pixel(ui.map_center,ui.map_zoom,
                          ui.start_touch_x,ui.start_touch_y,320,GUESS_MAP_HEIGHT));
        ui.touch_active=false;
    }
}
static void handle_play(u32 keys,u32 held,touchPosition touch,u64 now){
    if(keys&(KEY_B|KEY_START)){
        guess_toggle_pause(&ui.game,now);
        return;
    }
    if(keys&KEY_A){
        if(ui.game.has_pin){guess_submit(&ui.game);result_center();}
        return;
    }
    if(keys&KEY_X&&ui.map_zoom<17)ui.map_zoom++;
    if(keys&KEY_Y&&ui.map_zoom>0)ui.map_zoom--;
    handle_map_touch(keys,held,touch);
    if(keys&KEY_TOUCH&&touch.py>=199){
        if(touch.px>=175&&ui.game.has_pin){guess_submit(&ui.game);result_center();}
        else if(touch.px<175){ui.map_zoom=ui.map_zoom<17?ui.map_zoom+1:0;}
    }
    if(ui.game.rules!=GUESS_NMPZ){
        circlePosition stick;
        hidCircleRead(&stick);
        float x,y;
        street_look_axes(stick.dx,stick.dy,&x,&y);
        float dt=(float)(now-ui.last_ms)*.001f;
        if(dt>.05f)dt=.05f;
        ui.yaw+=(x + !!(held&KEY_DRIGHT)-!!(held&KEY_DLEFT))*1.7f*dt;
        ui.pitch+=y*1.7f*dt;
        if(keys&KEY_DUP&&ui.fov>28)ui.fov-=5;
        if(keys&KEY_DDOWN&&ui.game.rules!=GUESS_MOVING&&ui.fov<80)ui.fov+=5;
        if(ui.pitch>1.5533f)ui.pitch=1.5533f;
        if(ui.pitch< -1.5533f)ui.pitch=-1.5533f;
        ui.yaw=remainderf(ui.yaw,2*PI);
        if(keys&KEY_SELECT){
            if(ui.gyro)stop_gyro();
            else {
                ui.gyro=true;
                pano_gyro_reset(&ui.gyro_filter);
                if(R_FAILED(HIDUSER_EnableGyroscope())||
                   R_FAILED(HIDUSER_GetGyroscopeRawToDpsCoefficient(&ui.gyro_coeff))||
                   ui.gyro_coeff<=0)stop_gyro();
            }
        }
        if(ui.gyro){
            angularRate raw;hidGyroRead(&raw);
            pano_gyro_step(&ui.gyro_filter,raw.y,raw.z,
                           ui.gyro_coeff,dt,true,&ui.pitch,&ui.yaw);
        }
    }
    if(ui.game.rules==GUESS_MOVING){
        if(keys&(KEY_L|KEY_R)){
            int direction=keys&KEY_L?-1:1;
            pano_runtime_step_at(direction,ui.yaw);
        }
        if(keys&KEY_DDOWN&&ui.start_id[0]){
            pano_runtime_jump_game_google(ui.start_id,ui.game.start.lat,ui.game.start.lon);
        }
    }
    pano_runtime_poll();
    guess_tick(&ui.game,now);
    if(ui.game.phase==GUESS_RESULT)result_center();
    ui.last_ms=now;
}
void guess_ui_update(void){
    if(!ui.active)return;
    hidScanInput();
    u32 keys=hidKeysDown(),held=hidKeysHeld();
    touchPosition touch={0};hidTouchRead(&touch);
    u64 now=osGetTime();
    switch(ui.game.phase){
      case GUESS_MENU:handle_menu(keys,touch);break;
      case GUESS_FINDING:
        if(keys&KEY_B){drop_scene();ui.game.phase=GUESS_MENU;}
        else find_round(now);
        break;
      case GUESS_PLAYING:handle_play(keys,held,touch,now);break;
      case GUESS_RESULT:
        if(keys&KEY_A||((keys&KEY_TOUCH)&&touch.py>196)){
            if(guess_advance(&ui.game)){
                if(ui.game.phase==GUESS_COMPLETE){drop_scene();completed();}
                else reset_round_scene();
            }
        }else if(keys&KEY_B){drop_scene();ui.game.phase=GUESS_MENU;}
        break;
      case GUESS_COMPLETE:
        if(keys&KEY_A) new_game();
        else if(keys&KEY_B)ui.game.phase=GUESS_MENU;
        break;
      case GUESS_PAUSED:
        if(keys&KEY_A)guess_toggle_pause(&ui.game,now);
        else if(keys&KEY_B){drop_scene();ui.game.phase=GUESS_MENU;}
        break;
      case GUESS_ERROR:
        if(keys&KEY_A){ui.game.attempt=0;ui.game.phase=GUESS_FINDING;reset_round_scene();}
        else if(keys&KEY_B){drop_scene();ui.game.phase=GUESS_MENU;}
        break;
    }
}
static void draw_guess_map(C3D_RenderTarget *bottom,bool result){
    C2D_TargetClear(bottom,C2D_Color32(15,25,39,255));
    C2D_SceneBegin(bottom);
    map_tiles_set_coverage(false);
    map_tiles_render(ui.map_center.lat,ui.map_center.lon,ui.map_zoom,0,0,
                     320,GUESS_MAP_HEIGHT,false);
    if(ui.game.has_pin){
        float x,y;guess_map_project(ui.map_center,ui.game.pin,ui.map_zoom,320,GUESS_MAP_HEIGHT,&x,&y);
        if(x>=-20&&x<340&&y>=-20&&y<214){
            C2D_DrawCircleSolid(x,y,.9f,7,C2D_Color32(19,34,47,255));
            C2D_DrawCircleSolid(x,y,.91f,4,C2D_Color32(67,171,244,255));
        }
    }
    if(result){
        float x,y;guess_map_project(ui.map_center,ui.game.start,ui.map_zoom,320,GUESS_MAP_HEIGHT,&x,&y);
        if(ui.game.has_pin){
            float gx,gy;guess_map_project(ui.map_center,ui.game.pin,ui.map_zoom,320,GUESS_MAP_HEIGHT,&gx,&gy);
            C2D_DrawLine(gx,gy,C2D_Color32(55,204,243,235),x,y,C2D_Color32(241,90,95,235),3,1);
        }
        C2D_DrawCircleSolid(x,y,1,7,C2D_Color32(255,255,255,255));
        C2D_DrawCircleSolid(x,y,1,4,C2D_Color32(238,67,79,255));
    }
    C2D_DrawRectSolid(0,194,1,320,46,C2D_Color32(15,30,46,255));
    print(12,199,.31f,C2D_Color32(184,211,229,255),"X/Y map zoom  Touch pan/pin");
    if(result){
        char line[96];
        snprintf(line,sizeof(line),"%.1f km  +%u pts",ui.game.distances_km[ui.game.round],ui.game.score[ui.game.round]);
        print(12,219,.43f,C2D_Color32(236,247,251,255),line);
        print(244,219,.39f,C2D_Color32(95,226,174,255),"NEXT A");
    }else{
        C2D_DrawRectSolid(194,218,1.1f,115,20,C2D_Color32(ui.game.has_pin?31:57,ui.game.has_pin?135:71,ui.game.has_pin?86:82,255));
        print(207,220,.4f,C2D_Color32(255,255,255,255),ui.game.has_pin?"GUESS A":"PLACE PIN");
    }
}
void guess_ui_draw(C3D_RenderTarget *top,C3D_RenderTarget *bottom){
    if(!ui.active)return;
    if(!C3D_FrameBegin(C3D_FRAME_SYNCDRAW))return;
    C2D_TextBufClear(ui.text);
    const u32 fg=C2D_Color32(239,247,254,255),sub=C2D_Color32(154,195,218,255);
    bool pano=(ui.game.phase==GUESS_PLAYING||ui.game.phase==GUESS_RESULT||
               (ui.game.phase==GUESS_PAUSED&&ui.game.resume==GUESS_PLAYING))&&ui.scene_loaded;
    if(pano){
        explore_draw(top,ui.yaw,ui.pitch,3.9f,C3D_AngleFromDegrees(ui.fov),0);
        C2D_Prepare();C2D_SceneBegin(top);
        C2D_DrawRectSolid(0,0,.93f,400,22,C2D_Color32(8,15,25,203));
        char title[85];
        snprintf(title,sizeof(title),"STREETGUESS     %u / %u      %u pts",ui.game.round+1,GUESS_ROUNDS,ui.game.total);
        print(12,4,.42f,fg,title);
        /* Minimal top-screen compass. Its heading follows the view, while the
         * independent guess map never follows the secret panorama position. */
        const float compass=-ui.yaw;
        C2D_DrawCircleSolid(371,46,.96f,15,C2D_Color32(6,15,27,185));
        C2D_DrawLine(371,46,C2D_Color32(240,111,105,255),
                     371+sinf(compass)*12,46-cosf(compass)*12,
                     C2D_Color32(240,111,105,255),3,1);
        print(367,62,.27f,C2D_Color32(227,245,252,255),"N");
        const PanoLiveImage *credit=pano_runtime_current();
        print(11,224,.29f,fg,
              credit&&credit->credit[0]?credit->credit:"Imagery: Google Street View");
        if(ui.game.phase==GUESS_PLAYING&&ui.game.time_limit_ms){
            char time[25];unsigned left=(ui.game.remaining_ms+999)/1000;
            snprintf(time,sizeof(time),"%02u:%02u",left/60,left%60);
            print(341,224,.39f,left<11?C2D_Color32(255,104,99,255):fg,time);
        }
        if(ui.game.phase==GUESS_RESULT){
            C2D_DrawRectSolid(20,70,.94f,360,90,C2D_Color32(5,15,25,220));
            char line[85];snprintf(line,sizeof(line),"+%u points",ui.game.score[ui.game.round]);
            print(95,87,.72f,C2D_Color32(96,224,170,255),line);
            snprintf(line,sizeof(line),"Actual: %.3f, %.3f",ui.game.start.lat,ui.game.start.lon);
            print(92,129,.35f,sub,line);
        }
        if(ui.game.phase==GUESS_PAUSED){
            C2D_DrawRectSolid(30,60,.96f,340,120,C2D_Color32(6,17,29,240));
            print(135,81,.75f,fg,"PAUSED");
            print(75,127,.43f,sub,"A resume   B quit to menu");
        }
    }else{
        C2D_Prepare();C2D_TargetClear(top,C2D_Color32(12,24,40,255));
        C2D_SceneBegin(top);
        print(44,53,.94f,C2D_Color32(109,227,245,255),"STREETGUESS");
        print(78,93,.44f,fg,"A geography game for 3DS");
        C2D_DrawLine(48,128,C2D_Color32(67,169,202,255),355,128,C2D_Color32(67,169,202,255),2,.6f);
        if(ui.game.phase==GUESS_FINDING){
            char line[75];snprintf(line,sizeof(line),"Searching Google: %u/%u",ui.game.attempt,GUESS_MAX_CANDIDATE_TRIES);
            print(92,159,.46f,fg,line);
        }else if(ui.game.phase==GUESS_ERROR){
            print(38,151,.38f,C2D_Color32(248,117,113,255),ui.note);
        }else if(ui.game.phase==GUESS_COMPLETE){
            char line[80];snprintf(line,sizeof(line),"FINAL SCORE: %u / 25000",ui.game.total);
            print(79,155,.59f,C2D_Color32(120,245,183,255),line);
        }else print(79,159,.44f,sub,"Explore the world. Find where you are.");
    }
    if(ui.game.phase==GUESS_PLAYING||ui.game.phase==GUESS_RESULT||
       (ui.game.phase==GUESS_PAUSED&&ui.game.resume==GUESS_PLAYING)){
        draw_guess_map(bottom,ui.game.phase==GUESS_RESULT);
        if(ui.game.phase==GUESS_PAUSED){
            C2D_SceneBegin(bottom);
            C2D_DrawRectSolid(0,0,1.1f,320,240,C2D_Color32(4,10,19,190));
        }
    }else{
        C2D_TargetClear(bottom,C2D_Color32(13,24,37,255));
        C2D_SceneBegin(bottom);
        char name[92];
        switch(ui.game.phase){
          case GUESS_MENU:
            card(40,"PLAY STREETGUESS","A / tap to start five rounds");
            snprintf(name,sizeof(name),"Map: %s",region_names[ui.game.region]);
            card(83,name,"Tap to change region");
            snprintf(name,sizeof(name),"Rules: %s",rule_names[ui.game.rules]);
            card(123,name,"Tap to change moving rules");
            if(times[ui.time_choice])snprintf(name,sizeof(name),"Time: %u seconds",times[ui.time_choice]);
            else snprintf(name,sizeof(name),"Time: Unlimited");
            card(163,name,"Tap to change timer");
            if(ui.record_screen){
                snprintf(name,sizeof(name),"Games %u   Best %u",ui.games,ui.best[ui.game.region][ui.game.rules]);
                card(203,"RECORDS",name);
            }else card(203,"RECORDS","Tap to show personal best");
            break;
          case GUESS_FINDING:
            snprintf(name,sizeof(name),"Round %u of %u",ui.game.round+1,GUESS_ROUNDS);
            card(45,name,"Finding a playable Google panorama");
            card(100,"Searching live imagery","No fixed list of panoramas");
            card(155,"Please remain connected","B cancels and returns to menu");
            break;
          case GUESS_ERROR:
            card(40,"Could not start this round",ui.note);
            card(102,"A: Try again","B: Back to the game menu");
            break;
          case GUESS_COMPLETE:
            for(unsigned i=0;i<GUESS_ROUNDS;i++){
                snprintf(name,sizeof(name),"Round %u: %u points",i+1,ui.game.score[i]);
                print(18,29+i*32,.5f,fg,name);
            }
            card(201,"A: Play again","B: Game menu");
            break;
          default: break;
        }
    }
    C3D_FrameEnd(0);
}