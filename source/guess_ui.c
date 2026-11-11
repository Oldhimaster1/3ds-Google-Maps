#include "guess_ui.h"
#include "guess_core.h"
#include "guess_challenge.h"
#include <time.h>
#include "guess_roads.h"
#include "guess_replay.h"
#include "street_heading.h"
#include "earth_options.h"
#include "input_remap.h"
#include "detail_runtime.h"
#include "pano_runtime.h"
#include "explore.h"
#include "map_tiles.h"
#include "pano_gyro.h"
#include "pano_motion.h"
#include "ui_accessibility.h"
#include "street_look_input.h"
#include <citro2d.h>
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#define PI 3.14159265358979323846f
#define GUESS_SAVES "sdmc:/3ds_google_maps/streetguess_records.txt"
#define GUESS_ROADS_FILE "sdmc:/3ds_google_maps/streetguess_roads.bin"
#define GUESS_MAP_HEIGHT 194

typedef struct {
    bool active, scene_loaded, fetching, touch_active, touch_dragged, gyro;
    bool gyro_accel, map_focused, tutorial_open;
    int map_zoom, time_choice, last_touch_x, last_touch_y, start_touch_x, start_touch_y;
    float yaw,pitch,fov,gyro_coeff;
    PanoGyroFilter gyro_filter;
    PanoMotionFrame motion;
    u64 last_ms, next_search_ms;
    GuessPoint candidate, map_center;
    char start_id[48], note[96];
    GuessGame game;
    GuessRoads roads;
    GuessReplay replay;
    EarthOptions options;
    float last_azimuth;
    char last_view_id[48];
    uint64_t road_rng;
    unsigned best[GUESS_MAP_COUNT][GUESS_RULE_COUNT], games;
    C2D_TextBuf text;
    bool record_screen, challenge_menu, handoff, history_screen;
    unsigned mode, difficulty, players, player;
    unsigned player_scores[4];
    GuessChallenge challenge;
    char challenge_code[40];
    uint32_t challenge_day;
    unsigned daily_best, weekly_total, weekly_best, streak;
    char daily_history[7][48];
    unsigned recent[GUESS_ROUNDS], recent_count;

} GuessUi;
static GuessUi ui;
static const char *modes[]={"Classic random", "Daily challenge", "Friend challenge", "Pass and play", "Surprise me", "Mystery expedition", "Country streak"};
#define DAILY_FILE "sdmc:/3ds_google_maps/streetguess_daily_v2.dat"
static void challenge_records(bool save){
    uint32_t days[180]={0};unsigned levels[180]={0},scores[180]={0},count=0;
    FILE *f=fopen(DAILY_FILE,"r");
    if(f){unsigned version=0;if(fscanf(f,"SGD %u",&version)==1&&version==1){
        unsigned long day;unsigned level,score;
        while(count<180&&fscanf(f,"%lu %u %u",&day,&level,&score)==3){
            if(day>UINT32_MAX||level>2||score>25000){count=0;break;}
            days[count]=(uint32_t)day;levels[count]=level;scores[count++]=score;
        }
    }fclose(f);}
    if(save){
        unsigned i=0;for(;i<count;i++)if(days[i]==ui.challenge_day&&levels[i]==ui.difficulty)break;
        if(i==count){if(count==180){memmove(days,days+1,179*sizeof(*days));memmove(levels,levels+1,179*sizeof(*levels));memmove(scores,scores+1,179*sizeof(*scores));count=179;}i=count++;days[i]=ui.challenge_day;levels[i]=ui.difficulty;scores[i]=0;}
        if(ui.game.total>scores[i])scores[i]=ui.game.total;
        f=fopen(DAILY_FILE ".tmp","w");
        if(f){bool ok=fprintf(f,"SGD 1\n")>0;
            for(unsigned j=0;j<count;j++)if(fprintf(f,"%lu %u %u\n",(unsigned long)days[j],levels[j],scores[j])<=0)ok=false;
            if(fclose(f)!=0)ok=false;
            if(ok)rename(DAILY_FILE ".tmp",DAILY_FILE);else remove(DAILY_FILE ".tmp");
        }
    }
    ui.daily_best=ui.weekly_total=ui.weekly_best=ui.streak=0;
    for(unsigned back=0;back<7;back++){
        uint32_t target=ui.challenge_day>=back?ui.challenge_day-back:0;unsigned best=0;bool played=false;
        for(unsigned i=0;i<count;i++)if(days[i]==target&&levels[i]==ui.difficulty){played=true;if(scores[i]>best)best=scores[i];}
        time_t stamp=(time_t)((uint64_t)target*86400u);struct tm *date=gmtime(&stamp);
        if(date)snprintf(ui.daily_history[back],48,"%04d-%02d-%02d  %s%u",date->tm_year+1900,date->tm_mon+1,date->tm_mday,played?"":"unplayed / ",best);
    }
    for(unsigned i=0;i<count;i++)if(levels[i]==ui.difficulty){unsigned total=0;
        for(unsigned j=0;j<count;j++)if(levels[j]==ui.difficulty&&(days[j]+3)/7==(days[i]+3)/7)total+=scores[j];
        if(total>ui.weekly_best)ui.weekly_best=total;
    }
    for(unsigned i=0;i<count;i++)if(levels[i]==ui.difficulty){
        if(days[i]==ui.challenge_day)ui.daily_best=scores[i];
        if((days[i]+3)/7==(ui.challenge_day+3)/7)ui.weekly_total+=scores[i];
    }
    uint32_t day=ui.challenge_day;
    for(unsigned step=0;step<60&&day>0;step++,day--){bool found=false;
        for(unsigned i=0;i<count;i++)if(days[i]==day&&levels[i]==ui.difficulty)found=true;
        if(!found){if(step==0)continue;break;}ui.streak++;
    }
}
static const unsigned times[]={0,30,60,120,180,300};
static const char *region_names[]={"World","United States","Europe"};
static const char *rule_names[]={"Moving","No Move","NMPZ"};
static void print(float x,float y,float scale,u32 color,const char *s) {
    if (!ui.text || !s || !*s)return;
    const UiAccessConfig *cfg=ui_access_get();
    if(cfg->high_contrast){
        unsigned r=color&255u,g=(color>>8)&255u,b=(color>>16)&255u;
        unsigned maximum=r>g?(r>b?r:b):(g>b?g:b);
        if(maximum>=120 && maximum<220 && ((color>>24)&255u)>195)
            color=C2D_Color32(221,237,247,(color>>24)&255u);
    }
    if(scale<.65f)scale*=ui_access_scale();
    C2D_Text t;
    C2D_TextParse(&t,ui.text,s);
    C2D_TextOptimize(&t);
    /* Dialogs are near z=.95; text at .85 was hidden behind them. */
    C2D_DrawText(&t,C2D_WithColor,x,y,.99f,scale,scale,color);
    if(cfg->weight>0)
        C2D_DrawText(&t,C2D_WithColor,x+(cfg->weight==2?.7f:.35f),y,.991f,
                     scale,scale,color);
}
static void print_hint(float x,float y,float scale,u32 color,const char *s){
 char hint[256];input_remap_hint(REMAP_GUESS,s,hint,sizeof(hint));print(x,y,scale,color,hint);
}
static void card(float y,const char *heading,const char *detail){
    C2D_DrawRectSolid(10,y,.25f,300,35,C2D_Color32(25,40,58,255));
    C2D_DrawRectSolid(10,y,.26f,3,35,C2D_Color32(80,202,227,255));
    print_hint(19,y+3,.48f,C2D_Color32(242,247,253,255),heading);
    if(detail)print_hint(19,y+19,.36f,C2D_Color32(209,232,247,255),detail);
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
    if(ui.gyro_accel) HIDUSER_DisableAccelerometer();
    ui.gyro=ui.gyro_accel=false;
}
static void drop_scene(void){
    stop_gyro();
    detail_runtime_stop();
    pano_runtime_stop();
    if(ui.scene_loaded){explore_unload();ui.scene_loaded=false;}
    ui.fetching=false;
}
void guess_ui_leave(void){
    if(!ui.active)return;
    drop_scene();
    pano_runtime_end_game_google();
    guess_roads_close(&ui.roads);
    guess_replay_save(&ui.replay,GUESS_REPLAY_FILE);
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
    ui.players=2;
    ui.time_choice=3;
    ui.fov=55;
    ui.text=C2D_TextBufNew(4096);
    if(!ui.text)return false;
    ui.active=true;
    ui.tutorial_open=true;
    /* Clear queued ordinary-map prefetch jobs; the live Google round is
     * more important than invisible speculative map tiles. Keep RAM cache. */
    map_tiles_view_changed();
    ui.last_ms=osGetTime();
    ui.road_rng=ui.game.rng^UINT64_C(0x6a09e667f3bcc909);
    /* Optional road geometry lives on SD; if absent, the self-contained
     * global geographic sampler remains fully functional. */
    guess_roads_open(&ui.roads,GUESS_ROADS_FILE);
    guess_replay_clear(&ui.replay);
    guess_replay_load(&ui.replay,GUESS_REPLAY_FILE);
    earth_options_default(&ui.options);
    earth_options_load(&ui.options,EARTH_OPTIONS_FILE);
    ui.game.sampler=ui.options.sampler;
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
    ui.map_focused=false;
}
static void new_game(void){
    drop_scene();
    ui.game.sampler=ui.options.sampler;
    if(ui.mode){
        if(!ui.challenge_code[0] || ui.mode==1 || ui.mode==4 || ui.mode==5 || ui.mode==6){
            uint32_t seed=ui.mode==1?guess_challenge_day((int64_t)time(NULL)):
                (uint32_t)ui.game.rng^(uint32_t)svcGetSystemTick()^(uint32_t)osGetTime();
            if(ui.mode!=1)ui.game.rng+=UINT64_C(0x9e3779b97f4a7c15);
            seed=ui.mode==5?GUESS_EXPEDITION_SEED|(seed&0x00FFFFFFu):seed&0x7FFFFFFFu;
            ui.challenge_day=guess_challenge_day((int64_t)time(NULL));
            if(!guess_challenge_make(&ui.challenge,seed,ui.difficulty)){
                snprintf(ui.note,sizeof(ui.note),"Challenge dataset unavailable");guess_error(&ui.game);return;
            }
            /* Daily/friend/party rounds are fixed. Solo discovery avoids the last
             * batch when the curated pool has room, without changing shared replays. */
            if(ui.mode>=4){
                GuessChallenge best=ui.challenge;unsigned least=GUESS_ROUNDS+1;
                for(unsigned attempt=0;attempt<64;attempt++){
                    GuessChallenge candidate;guess_challenge_make(&candidate,seed,ui.difficulty);
                    unsigned overlap=0;
                    for(unsigned a=0;a<GUESS_ROUNDS;a++)for(unsigned b=0;b<ui.recent_count;b++)
                        if(candidate.location[a]==ui.recent[b])overlap++;
                    if(overlap<least){best=candidate;least=overlap;}if(!overlap)break;
                    seed=ui.mode==5?GUESS_EXPEDITION_SEED|((seed+1)&0x00FFFFFFu):(seed+1)&0x7FFFFFFFu;
                }
                ui.challenge=best;memcpy(ui.recent,best.location,sizeof(ui.recent));ui.recent_count=GUESS_ROUNDS;
            }
            guess_challenge_code(&ui.challenge,ui.challenge_code,sizeof(ui.challenge_code));
        }
        ui.difficulty=ui.challenge.difficulty;
        GuessRules rules=ui.difficulty==2?GUESS_NMPZ:GUESS_NO_MOVE;
        guess_begin(&ui.game,GUESS_WORLD,rules,ui.difficulty==0||ui.mode==6?0:ui.difficulty==1?120:60);
        /* Exact daily/friend rounds deliberately replay the same locations.
         * The random game's history persists across ordinary games only. */
        ui.game.history_size=ui.game.history_pos=0;
        challenge_records(false);
    }else guess_begin(&ui.game,ui.game.region,ui.game.rules,times[ui.time_choice]);
    reset_guess_map();
    ui.yaw=0;ui.pitch=0;ui.fov=55;
    ui.start_id[0]=0;
    ui.last_view_id[0]=0;
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
        if(ui.mode){
            const GuessLocation *location=&guess_locations[ui.challenge.location[ui.game.round]];
            ui.candidate=(GuessPoint){location->lat,location->lon};
            if(!pano_runtime_start_curated(location->id,location->metadata_url)){
                snprintf(ui.note,sizeof(ui.note),"Exact challenge image unavailable; retry");guess_error(&ui.game);return;
            }
            ui.fetching=true;
        }else{
        if(!guess_random_candidate(&ui.game,&ui.candidate)){
            snprintf(ui.note,sizeof(ui.note),"No playable Google panorama found. Retry or exit.");
            guess_error(&ui.game);return;
        }
        /* Prefer independently sourced road geometry when a valid pack is
         * installed. Never store Google's panorama IDs or create an index of
         * Google locations. Reject near-repeats before selecting the road. */
        GuessPoint road;
        if(ui.roads.file&&guess_roads_sample(&ui.roads,&ui.road_rng,
                                            ui.game.region,&road)&&
           guess_candidate_acceptable(&ui.game,road))
            ui.candidate=road;
        /* Reject recently visited geographic cells BEFORE an HTTP lookup.
         * This does not store or index Google's imagery. */
        if(!guess_candidate_acceptable(&ui.game,ui.candidate) ||
           !guess_replay_can_play(&ui.replay,ui.candidate.lat,ui.candidate.lon,1)){
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
    }
    pano_runtime_poll();
    if(pano_runtime_has_live()){
        const PanoLiveImage *p=pano_runtime_current();
        GuessPoint actual={p->lat,p->lon};
        /* Avoid surprising cross-region images when a request snaps to a road
         * far from the sampled point. The true panorama position is scored. */
        bool identity=ui.mode ? p->provider==STREET_PROVIDER_PANORAMAX &&
            !strcmp(p->id,guess_locations[ui.challenge.location[ui.game.round]].id) :
            p->provider==STREET_PROVIDER_GOOGLE;
        bool good=identity&&p->is_pano&&
                  guess_distance_km(ui.candidate,actual)<40.0&&
                  (ui.mode||guess_replay_can_play(&ui.replay,actual.lat,actual.lon,1))&&
                  guess_accept_start(&ui.game,actual,now);
        if(good){
            guess_replay_add(&ui.replay,actual.lat,actual.lon);
            guess_replay_save(&ui.replay,GUESS_REPLAY_FILE);
            snprintf(ui.last_view_id,sizeof(ui.last_view_id),"%s",p->id);
            ui.last_azimuth=p->azimuth;
            snprintf(ui.start_id,sizeof(ui.start_id),"%s",p->id);
            /* Deterministic per-round random initial direction, not always north. */
            ui.yaw=(float)(((ui.game.rng>>17)%360)*PI/180.0f);
            ui.fetching=false;
            ui.last_ms=now;
            return;
        }
        pano_runtime_stop();
        ui.fetching=false;
        if(ui.mode){snprintf(ui.note,sizeof(ui.note),"Challenge image identity changed; retry");guess_error(&ui.game);return;}
        ui.next_search_ms=now+150;
    }else if(!pano_runtime_network_active()){
        const char *reason=pano_runtime_status();
        if(strstr(reason,"rate limited")||strstr(reason,"denied access")){
            snprintf(ui.note,sizeof(ui.note),"Google declined requests; try again later");
            guess_error(&ui.game);
        }else if(ui.mode){
            snprintf(ui.note,sizeof(ui.note),"Exact image unavailable; no round lost");
            guess_error(&ui.game);ui.fetching=false;
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
    if(ui.mode){
        ui.player_scores[ui.player]=ui.game.total;
        if(ui.mode==1)challenge_records(true);
        return;
    }
    ui.games++;
    unsigned *best=&ui.best[ui.game.region][ui.game.rules];
    if(ui.game.total>*best)*best=ui.game.total;
    save_records();
}
static void handle_menu(u32 keys,touchPosition touch){
    if(keys&KEY_B){guess_ui_leave();return;}
    if(keys&KEY_SELECT){if(ui.history_screen){ui.history_screen=false;return;}ui.challenge_menu=!ui.challenge_menu;return;}
    if(ui.challenge_menu){
        int row=(keys&KEY_TOUCH)&&touch.py>=40&&touch.py<240?(touch.py-40)/40:-1;
        if(keys&KEY_X || row==0){ui.mode=(ui.mode+1)%7;ui.challenge_code[0]=0;}
        else if(keys&KEY_Y || row==1){ui.difficulty=(ui.difficulty+1)%3;ui.challenge_code[0]=0;}
        else if(row==2)ui.players=2+(ui.players-1)%3;
        else if(row==3){
            SwkbdState keyboard;char code[40]={0};
            swkbdInit(&keyboard,SWKBD_TYPE_NORMAL,2,39);
            swkbdSetHintText(&keyboard,"Paste or type a friend challenge code");
            if(swkbdInputText(&keyboard,code,sizeof(code))==SWKBD_BUTTON_CONFIRM){
                if(guess_challenge_parse(&ui.challenge,code)){
                    guess_challenge_code(&ui.challenge,ui.challenge_code,sizeof(ui.challenge_code));ui.mode=2;ui.difficulty=ui.challenge.difficulty;
                    snprintf(ui.note,sizeof(ui.note),"Friend code accepted");
                }else snprintf(ui.note,sizeof(ui.note),"Invalid code or different dataset");
            }
        }else if(row==4){ui.challenge_day=guess_challenge_day((int64_t)time(NULL));challenge_records(false);ui.history_screen=!ui.history_screen;}
        if(keys&KEY_A){ui.player=0;memset(ui.player_scores,0,sizeof(ui.player_scores));new_game();}
        return;
    }
    if(keys&KEY_Y){ui.tutorial_open=!ui.tutorial_open;return;}
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
        if(!ui.touch_dragged&&ui.game.phase==GUESS_PLAYING){
            ui.map_focused=true;
            guess_set_pin(&ui.game,guess_map_at_pixel(ui.map_center,ui.map_zoom,
                          ui.start_touch_x,ui.start_touch_y,320,GUESS_MAP_HEIGHT));
        }
        ui.touch_active=false;
    }
}
static void handle_play(u32 keys,u32 held,touchPosition touch,u64 now){
    if(keys&KEY_START || ((keys&KEY_B)&&!ui.map_focused)){
        guess_toggle_pause(&ui.game,now);
        return;
    }
    if(keys&KEY_B && ui.map_focused){ui.map_focused=false;return;}
    if(ui.mode==6 && (keys&KEY_TOUCH) && touch.py>=199)keys|=KEY_A;
    if(keys&KEY_A && ui.mode==6){
        SwkbdState keyboard;char answer[24]={0};
        swkbdInit(&keyboard,SWKBD_TYPE_NORMAL,2,23);
        swkbdSetHintText(&keyboard,"Country name or 2-letter country code");
        if(swkbdInputText(&keyboard,answer,sizeof(answer))==SWKBD_BUTTON_CONFIRM){
            const GuessLocation *location=&guess_locations[ui.challenge.location[ui.game.round]];
            char country[3];
            if(!guess_country_parse(answer,country)){
                snprintf(ui.note,sizeof(ui.note),"Enter a country name or two-letter code");return;
            }
            bool correct=!strcmp(location->country,country);
            unsigned previous=ui.game.total;
            guess_set_pin(&ui.game,ui.game.start);guess_submit(&ui.game);
            ui.game.score[ui.game.round]=correct?1:0;ui.game.total=previous+(correct?1:0);
            result_center();
        }
        return;
    }
    if(keys&KEY_A){
        if(!ui.map_focused){
            ui.map_focused=true;
            snprintf(ui.note,sizeof(ui.note),"Tap map to place your pin");
        }else if(ui.game.has_pin){guess_submit(&ui.game);result_center();}
        else snprintf(ui.note,sizeof(ui.note),"Tap a location, then press A to guess");
        return;
    }
    if(keys&KEY_X&&ui.map_zoom<17)ui.map_zoom++;
    if(keys&KEY_Y&&ui.map_zoom>0)ui.map_zoom--;
    if(ui.mode!=6)handle_map_touch(keys,held,touch);
    if(ui.mode!=6 && (keys&KEY_TOUCH)&&touch.py>=199){
        if(touch.px>=175){
            ui.map_focused=true;
            if(ui.game.has_pin){guess_submit(&ui.game);result_center();}
        } else if(touch.px<175){
            if(ui.game.rules==GUESS_MOVING&&ui.start_id[0])
                pano_runtime_jump_game_google(ui.start_id,ui.game.start.lat,ui.game.start.lon);
            else ui.map_zoom=ui.map_zoom<17?ui.map_zoom+1:0;
        }
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
        if(keys&KEY_DDOWN&&ui.fov<80)ui.fov+=5;
        if(ui.pitch>1.5533f)ui.pitch=1.5533f;
        if(ui.pitch< -1.5533f)ui.pitch=-1.5533f;
        ui.yaw=remainderf(ui.yaw,2*PI);
        if(keys&KEY_SELECT){
            if(ui.gyro)stop_gyro();
            else {
                ui.gyro=R_SUCCEEDED(HIDUSER_EnableGyroscope());
                ui.gyro_accel=ui.gyro &&
                    R_SUCCEEDED(HIDUSER_EnableAccelerometer());
                pano_gyro_reset(&ui.gyro_filter);
                pano_motion_reset(&ui.motion);
                if(ui.gyro &&
                   (R_FAILED(HIDUSER_GetGyroscopeRawToDpsCoefficient(&ui.gyro_coeff))||
                    ui.gyro_coeff<=0))stop_gyro();
            }
        }
        if(ui.gyro){
            angularRate raw;hidGyroRead(&raw);
            accelVector acc={0};
            if(ui.gyro_accel) hidAccelRead(&acc);
            float pitch_raw=0,yaw_raw=0;
            if(pano_motion_project(&ui.motion,raw.x,raw.y,raw.z,
                                    acc.x,acc.y,acc.z,&pitch_raw,&yaw_raw))
                pano_gyro_step(&ui.gyro_filter,pitch_raw,yaw_raw,
                               ui.gyro_coeff,dt,true,&ui.pitch,&ui.yaw);
        }
    }
    if(ui.game.rules==GUESS_MOVING){
        if(keys&(KEY_L|KEY_R)){
            int direction=keys&KEY_L?-1:1;
            pano_runtime_step_at(direction,ui.yaw);
        }
    }
    pano_runtime_poll();
    const PanoLiveImage *after=pano_runtime_current();
    if(after && after->is_pano && after->id[0] &&
       strcmp(after->id,ui.last_view_id)!=0){
        float corrected=ui.yaw;
        if(ui.last_view_id[0] && street_heading_transfer(ui.last_azimuth,
                ui.yaw,after->azimuth,&corrected))ui.yaw=corrected;
        snprintf(ui.last_view_id,sizeof(ui.last_view_id),"%s",after->id);
        ui.last_azimuth=after->azimuth;
    }
    /* Provider latency must not consume a player's timed round. */
    if(pano_runtime_network_active())ui.game.last_tick=now;
    else guess_tick(&ui.game,now);
    if(ui.game.phase==GUESS_RESULT){ui.map_focused=false;result_center();}
    ui.last_ms=now;
}
void guess_ui_update(void){
    if(!ui.active)return;
    hidScanInput();
    u32 keys=input_remap_apply(REMAP_GUESS,hidKeysDown());
    u32 held=input_remap_apply(REMAP_GUESS,hidKeysHeld());
    touchPosition touch={0};hidTouchRead(&touch);
    u64 now=osGetTime();
    if(ui.handoff){if(keys&KEY_A){ui.handoff=false;new_game();}else if(keys&KEY_B){ui.handoff=false;ui.game.phase=GUESS_MENU;}return;}
    switch(ui.game.phase){
      case GUESS_MENU:handle_menu(keys,touch);break;
      case GUESS_FINDING:
        if(keys&KEY_B){drop_scene();ui.game.phase=GUESS_MENU;}
        else find_round(now);
        break;
      case GUESS_PLAYING:handle_play(keys,held,touch,now);break;
      case GUESS_RESULT:
        if(keys&KEY_A||((keys&KEY_TOUCH)&&touch.py>196)){
            if(ui.mode==6 && ui.game.score[ui.game.round]==0){ui.game.phase=GUESS_COMPLETE;drop_scene();completed();break;}
            if(guess_advance(&ui.game)){
                if(ui.game.phase==GUESS_COMPLETE){
                    if(ui.mode==6){unsigned total=ui.game.total;new_game();ui.game.total=total;}
                    else{drop_scene();completed();}
                }
                else reset_round_scene();
            }
        }else if(keys&KEY_B){drop_scene();ui.game.phase=GUESS_MENU;}
        break;
      case GUESS_COMPLETE:
        if(keys&KEY_A){
            if(ui.mode==3 && ui.player+1<ui.players){ui.player++;ui.handoff=true;ui.game.phase=GUESS_MENU;}
            else{ui.player=0;new_game();}
        }
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
    if(ui.scene_loaded && (ui.game.phase==GUESS_PLAYING ||
        ui.game.phase==GUESS_RESULT ||
        (ui.game.phase==GUESS_PAUSED && ui.game.resume==GUESS_PLAYING)))
        detail_runtime_update_panorama(pano_runtime_current(),ui.yaw,ui.pitch,
                                       ui.fov,ui.options.panorama_quality);
}
static void draw_guess_map(C3D_RenderTarget *bottom,bool result){
    C2D_TargetClear(bottom,C2D_Color32(15,25,39,255));
    C2D_SceneBegin(bottom);
    bool previous_coverage=map_tiles_get_coverage();
    map_tiles_set_coverage(false);
    map_tiles_render(ui.map_center.lat,ui.map_center.lon,ui.map_zoom,0,0,
                     320,GUESS_MAP_HEIGHT,false);
    map_tiles_set_coverage(previous_coverage);
    /* Always provide visible guidance even while online map tiles load. */
    if(!result){
        C2D_DrawRectSolid(0,0,.82f,320,27,C2D_Color32(10,23,39,224));
        print_hint(10,5,.40f,C2D_Color32(248,253,255,255),
              ui.mode==6?"A: NAME THE COUNTRY":ui.game.has_pin?"PIN SET - PRESS A OR GUESS":"TAP MAP TO PLACE YOUR PIN");
    }
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
    C2D_DrawRectSolid(0,194,.70f,320,46,C2D_Color32(15,30,46,255));
    print_hint(12,199,.38f,C2D_Color32(226,242,252,255),
          ui.map_focused?"B close map   X/Y zoom":"A focus map   X/Y zoom");
    if(!result&&ui.game.rules==GUESS_MOVING&&ui.start_id[0])
        print(12,220,.35f,C2D_Color32(120,228,245,255),"RETURN TO START");
    if(result){
        char line[96];
        if(ui.mode==6)snprintf(line,sizeof(line),"Country: %s | streak %u",guess_locations[ui.challenge.location[ui.game.round]].country,ui.game.total);
        else snprintf(line,sizeof(line),"%.1f km  +%u pts",ui.game.distances_km[ui.game.round],ui.game.score[ui.game.round]);
        print(12,219,.43f,C2D_Color32(236,247,251,255),line);
        print_hint(244,219,.39f,C2D_Color32(95,226,174,255),"NEXT A");
    }else{
        C2D_DrawRectSolid(194,218,.82f,115,20,C2D_Color32(ui.game.has_pin?31:57,ui.game.has_pin?135:71,ui.game.has_pin?86:82,255));
        print_hint(207,220,.40f,C2D_Color32(255,255,255,255),
              ui.game.has_pin?"GUESS / A":"TAP TO PIN");
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
        if(ui.mode==6)snprintf(title,sizeof(title),"COUNTRY STREAK     %u correct",ui.game.total);
        else snprintf(title,sizeof(title),"STREETGUESS     %u / %u      %u pts",ui.game.round+1,GUESS_ROUNDS,ui.game.total);
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
        char attribution[160];
        snprintf(attribution,sizeof(attribution),"%s | %s",credit&&credit->credit[0]?credit->credit:"Imagery provider",
                 credit?credit->license:"");
        print(11,224,.27f,fg,attribution);
        if(ui.mode && ui.difficulty==0 && ui.game.phase==GUESS_PLAYING)
            print(12,199,.30f,fg,guess_locations[ui.challenge.location[ui.game.round]].hint);
        if(ui.game.phase==GUESS_PLAYING&&ui.game.time_limit_ms){
            char time[25];unsigned left=(ui.game.remaining_ms+999)/1000;
            snprintf(time,sizeof(time),"%02u:%02u",left/60,left%60);
            print(341,224,.39f,left<11?C2D_Color32(255,104,99,255):fg,time);
        }
        if(ui.game.phase==GUESS_PLAYING && ui.map_focused){
            C2D_DrawRectSolid(18,67,.94f,364,67,C2D_Color32(5,15,25,216));
            print(38,78,.52f,fg,"PLACE YOUR GUESS BELOW");
            print_hint(38,109,.42f,fg,ui.game.has_pin?
                  "Pin ready: A to submit, B to look":"Tap map: place pin, B to look");
        }
        if(ui.game.phase==GUESS_RESULT){
            C2D_DrawRectSolid(20,70,.94f,360,90,C2D_Color32(5,15,25,220));
            char line[85];snprintf(line,sizeof(line),ui.mode==6?"%u correct country":"+%u points",ui.game.score[ui.game.round]);
            print(95,87,.72f,C2D_Color32(96,224,170,255),line);
            snprintf(line,sizeof(line),"Actual: %.3f, %.3f",ui.game.start.lat,ui.game.start.lon);
            print(92,129,.35f,sub,line);
            if(ui.mode)print(40,150,.35f,fg,guess_locations[ui.challenge.location[ui.game.round]].name);
        }
        if(ui.game.phase==GUESS_PAUSED){
            C2D_DrawRectSolid(30,60,.96f,340,120,C2D_Color32(6,17,29,240));
            print(135,81,.75f,fg,"PAUSED");
            print_hint(75,127,.43f,sub,"A resume   B quit to menu");
        }
    }else{
        C2D_Prepare();C2D_TargetClear(top,C2D_Color32(12,24,40,255));
        C2D_SceneBegin(top);
        print(44,53,.94f,C2D_Color32(109,227,245,255),"STREETGUESS");
        print(78,93,.44f,fg,"A geography game for 3DS");
        C2D_DrawLine(48,128,C2D_Color32(67,169,202,255),355,128,C2D_Color32(67,169,202,255),2,.6f);
        if(ui.game.phase==GUESS_FINDING){
            char line[75];snprintf(line,sizeof(line),ui.mode?"Loading challenge round %u/%u":"Searching Google: %u/%u",ui.mode?ui.game.round+1:ui.game.attempt,ui.mode?GUESS_ROUNDS:GUESS_MAX_CANDIDATE_TRIES);
            print(92,159,.46f,fg,line);
        }else if(ui.game.phase==GUESS_ERROR){
            print(38,151,.38f,C2D_Color32(248,117,113,255),ui.note);
        }else if(ui.game.phase==GUESS_COMPLETE){
            char line[80];snprintf(line,sizeof(line),"FINAL SCORE: %u / %u",ui.game.total,ui.mode==6?5u:25000u);
            print(79,155,.59f,C2D_Color32(120,245,183,255),line);
        }else if(ui.game.phase==GUESS_MENU && ui.tutorial_open){
            print(57,143,.48f,fg,"1. Look at mystery Street View");
            print(57,171,.48f,fg,"2. Tap the map to place a pin");
            print(57,199,.48f,fg,"3. Press A to GUESS");
        }else print(62,159,.44f,sub,"A play   Y help   B exit to Maps");
    }
    if(ui.game.phase==GUESS_PLAYING||ui.game.phase==GUESS_RESULT||
       (ui.game.phase==GUESS_PAUSED&&ui.game.resume==GUESS_PLAYING)){
        draw_guess_map(bottom,ui.game.phase==GUESS_RESULT);
        if(ui.game.phase==GUESS_PAUSED){
            C2D_SceneBegin(bottom);
            C2D_DrawRectSolid(0,0,.95f,320,240,C2D_Color32(4,10,19,210));
            print(92,68,.72f,C2D_Color32(255,255,255,255),"GAME PAUSED");
            print_hint(43,111,.47f,C2D_Color32(245,250,254,255),"A  Resume round");
            print_hint(43,144,.47f,C2D_Color32(245,250,254,255),"B  Back to game menu");
        }
    }else{
        C2D_TargetClear(bottom,C2D_Color32(13,24,37,255));
        C2D_SceneBegin(bottom);
        char name[92];
        switch(ui.game.phase){
          case GUESS_MENU:
            if(ui.handoff){
                snprintf(name,sizeof(name),"PASS TO PLAYER %u",ui.player+1);
                card(55,name,"A when ready. Other players look away.");break;
            }
            if(ui.challenge_menu && ui.history_screen){
                print_hint(16,8,.42f,fg,"UTC DAILY HISTORY | SELECT back");
                for(unsigned i=0;i<7;i++)print(16,35+i*22,.38f,fg,ui.daily_history[i]);
                snprintf(name,sizeof(name),"Week %u | Best week %u",ui.weekly_total,ui.weekly_best);
                print(16,201,.38f,fg,name);break;
            }
            if(ui.challenge_menu){
                snprintf(name,sizeof(name),"Mode: %s",modes[ui.mode]);card(40,name,"Tap / X change | A play | SELECT classic");
                snprintf(name,sizeof(name),"Difficulty: %s",ui.difficulty==0?"Explorer (hints)":ui.difficulty==1?"Standard":"Expert NMPZ");card(80,name,"Tap / Y change. Codes include rules.");
                snprintf(name,sizeof(name),"Players: %u",ui.players);card(120,name,"Tap: 2-4 players | Same places, same rules");
                card(160,"ENTER FRIEND CODE",ui.note[0]?ui.note:"Tap to enter an SG1 code");
                snprintf(name,sizeof(name),"Today %u | Week %u | Streak %u",ui.daily_best,ui.weekly_total,ui.streak);
                card(200,"UTC DAILY RECORDS",name);break;
            }
            card(40,"PLAY STREETGUESS","A to start  |  Y how to play");
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
            }else card(203,"RECORDS","Tap records | SELECT challenges");
            break;
          case GUESS_FINDING:
            snprintf(name,sizeof(name),"Round %u of %u",ui.game.round+1,GUESS_ROUNDS);
            card(45,name,ui.mode?"Loading exact licensed challenge image":"Finding a playable Google panorama");
            card(100,ui.mode?"No substitutions":"Searching live imagery",ui.mode?"Missing image: retry, no score penalty":"No fixed list of panoramas");
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
            if(ui.mode){
                print(15,7,.29f,fg,ui.mode==6?"COUNTRY STREAK":ui.challenge_code);
                if(ui.mode==3){
                    for(unsigned i=0;i<=ui.player;i++){
                        snprintf(name,sizeof(name),"P%u: %u",i+1,ui.player_scores[i]);
                        print(215,29+i*32,.40f,fg,name);
                    }
                }
            }
            card(201,ui.mode==3&&ui.player+1<ui.players?"A: Next player":"A: Play again","B: Game menu | Photograph score + code");
            break;
          default: break;
        }
    }
    C3D_FrameEnd(0);
}
