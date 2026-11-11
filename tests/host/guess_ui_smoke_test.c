/* Simulated 3DS control and provider harness. This deliberately does not
 * substitute for a real devkitPro build or hardware network test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include "guess_ui.h"
#include "guess_core.h"
#include "guess_challenge.h"
#include <citro2d.h>
#include <3ds.h>
#include "panorama_live.h"
#include "pano_gyro.h"
static u64 milliseconds=100000;
static u32 down,held;
static touchPosition touch;
static PanoLiveImage current;
static bool curated,network_busy;
static char curated_id[48];
static bool live,rendered,mock_denied,mock_no_coverage;
static int search_count,moves_count,finish_count,scene_count,return_count;
static double query_lat,query_lon,last_map_lat,last_map_lon;
struct C3D_RenderTarget { int n; };
struct C2D_TextBuf_s { int n; };

u64 svcGetSystemTick(void){return 0xacefeed3435ULL;}
u64 osGetTime(void){return milliseconds;}
void gfxSet3D(bool b){(void)b;}
void hidScanInput(void){}
u32 hidKeysDown(void){return down;}
u32 hidKeysHeld(void){return held;}
void hidTouchRead(touchPosition *p){*p=touch;}
void hidCircleRead(circlePosition *p){p->dx=145;p->dy=0;}
void hidGyroRead(angularRate *p){p->x=p->y=p->z=0;}
void hidAccelRead(accelVector *p){p->x=0;p->y=0;p->z=256;}
int HIDUSER_DisableGyroscope(void){return 0;}
int HIDUSER_EnableAccelerometer(void){return 0;}
int HIDUSER_DisableAccelerometer(void){return 0;}
int HIDUSER_EnableGyroscope(void){return 0;}
int HIDUSER_GetGyroscopeRawToDpsCoefficient(float *x){*x=10;return 0;}
void pano_gyro_reset(PanoGyroFilter *f){f->ready=0;}
bool pano_gyro_step(PanoGyroFilter *f,float a,float b,float c,float d,bool e,float *p,float *y){(void)f;(void)a;(void)b;(void)c;(void)d;(void)e;(void)p;(void)y;return true;}
void street_look_axes(int x,int y,float *a,float *b){*a=x/145.0f;*b=y/145.0f;}
C2D_TextBuf C2D_TextBufNew(size_t sz){(void)sz;return malloc(sizeof(struct C2D_TextBuf_s));}
void C2D_TextBufDelete(C2D_TextBuf p){free(p);}
void C2D_TextBufClear(C2D_TextBuf p){(void)p;}
void C2D_TextParse(C2D_Text *x,C2D_TextBuf t,const char*s){(void)x;(void)t;(void)s;}
void C2D_TextOptimize(C2D_Text *x){(void)x;}
bool C2D_DrawText(const C2D_Text *t,u32 f,float a,float b,float c,float d,float e,u32 color){(void)t;(void)f;(void)a;(void)b;(void)c;(void)d;(void)e;(void)color;return true;}
void C2D_Prepare(void){}
void C2D_TargetClear(C3D_RenderTarget *t,u32 color){(void)t;(void)color;}
void C2D_SceneBegin(C3D_RenderTarget *t){(void)t;}
u32 C2D_Color32(int r,int g,int b,int a){(void)r;(void)g;(void)b;(void)a;return 0;}
void C2D_DrawRectSolid(float a,float b,float c,float d,float e,u32 f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
void C2D_DrawCircleSolid(float a,float b,float c,float d,u32 e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void C2D_DrawLine(float a,float b,u32 c,float d,float e,u32 f,float g,float h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
bool C3D_FrameBegin(int n){(void)n;return true;}
void C3D_FrameEnd(int n){(void)n;}
float C3D_AngleFromDegrees(float n){return n*.01745329252f;}
static bool coverage_enabled=true;
void map_tiles_set_coverage(bool enabled){coverage_enabled=enabled;}
bool map_tiles_get_coverage(void){return coverage_enabled;}
static unsigned map_cancel_count;
void map_tiles_view_changed(void){++map_cancel_count;}
void map_tiles_render(double lat,double lon,int z,int ox,int oy,int w,int h,bool top){
    (void)z;(void)ox;(void)oy;(void)w;(void)h;(void)top;
    last_map_lat=lat;last_map_lon=lon;
}
bool explore_load(int mode,unsigned n){(void)mode;(void)n;scene_count++;return true;}
void explore_unload(void){scene_count--;}
void explore_draw(C3D_RenderTarget *p,float yaw,float pitch,float d,float f,float e){
    (void)p;(void)yaw;(void)pitch;(void)d;(void)f;(void)e;rendered=true;
}
bool pano_runtime_start_game_google(double lat,double lon){
    curated=false;query_lat=lat;query_lon=lon;live=false;
    search_count++;return true;
}
bool pano_runtime_start_curated(const char *id,const char *url){
 (void)url;curated=true;strcpy(curated_id,id);live=false;
 for(unsigned i=0;i<guess_location_count;i++)if(!strcmp(guess_locations[i].id,id)){
  query_lat=guess_locations[i].lat;query_lon=guess_locations[i].lon;return true;
 }return false;
}
void swkbdInit(SwkbdState *s,int a,int b,int c){(void)s;(void)a;(void)b;(void)c;}
void swkbdSetHintText(SwkbdState *s,const char *h){(void)s;(void)h;}
static char keyboard_answer[24];
int swkbdInputText(SwkbdState *s,char *c,size_t n){(void)s;if(!keyboard_answer[0])return 0;snprintf(c,n,"%s",keyboard_answer);return SWKBD_BUTTON_CONFIRM;}
void pano_runtime_end_game_google(void){finish_count++;live=false;}
void pano_runtime_stop(void){live=false;}
void pano_runtime_poll(void){
    if(mock_denied||mock_no_coverage)return;
    if(!live){memset(&current,0,sizeof(current));current.provider=curated?STREET_PROVIDER_PANORAMAX:STREET_PROVIDER_GOOGLE;
        current.lat=query_lat;current.lon=query_lon;current.is_pano=true;
        strcpy(current.id,curated?curated_id:"1234567890123456789012");current.azimuth=0;live=true;}
}
bool pano_runtime_has_live(void){return live;}
bool pano_runtime_network_active(void){return network_busy;}
const char *pano_runtime_status(void){return mock_denied?"Imagery provider denied access (HTTP 403)":"No matching photos";}
const PanoLiveImage *pano_runtime_current(void){return live?&current:NULL;}
bool pano_runtime_step_at(int direction,float yaw){(void)direction;(void)yaw;moves_count++;return true;}
bool pano_runtime_jump_game_google(const char *id,double lat,double lon){(void)id;(void)lat;(void)lon;return_count++;return true;}
void input_remap_hint(unsigned p,const char *s,char *o,size_t n){(void)p;snprintf(o,n,"%s",s);}
uint32_t input_remap_apply(unsigned p,uint32_t k){(void)p;return k;}
void detail_runtime_stop(void){}
const char *detail_runtime_status(void){return "";}
void detail_runtime_update_panorama(const PanoLiveImage *p,float y,float t,float f,unsigned q){(void)p;(void)y;(void)t;(void)f;(void)q;}
#include "../../source/guess_ui.c"
#define TEST(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void press(u32 k){down=k;held=k;milliseconds+=16;guess_ui_update();down=held=0;}
static void frame(void){milliseconds+=16;guess_ui_update();}
static void emit_le32(unsigned char *p,unsigned v){
    p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);
    p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);
}
static void install_fake_road_pack(void){
    FILE *f=fopen("sdmc:/3ds_google_maps/streetguess_roads.bin","wb");
    TEST(f!=NULL);
    unsigned char hdr[16]={0};
    memcpy(hdr,"SGROAD1\0",8);
    emit_le32(hdr+8,1);emit_le32(hdr+12,20);
    TEST(fwrite(hdr,1,16,f)==16);
    unsigned char segment[20]={0};
    emit_le32(segment+0,40000000);
    emit_le32(segment+4,(unsigned)-74000000);
    emit_le32(segment+8,40001000);
    emit_le32(segment+12,(unsigned)-73999000);
    emit_le32(segment+16,1|2);
    TEST(fwrite(segment,1,20,f)==20);
    fclose(f);
}
static void pick_pin(void){
    down=held=KEY_TOUCH;touch=(touchPosition){.px=170,.py=95};frame();
    down=held=0;frame();
    TEST(ui.game.has_pin);
}
static void play_all(void){
    TEST(ui.game.phase==GUESS_FINDING);
    int tries=0;
    while(ui.game.phase==GUESS_FINDING&&tries++<10)frame();
    TEST(ui.game.phase==GUESS_PLAYING);
    TEST(ui.game.start.lat==current.lat&&ui.game.start.lon==current.lon);
    TEST(ui.map_center.lat!=ui.game.start.lat||ui.map_center.lon!=ui.game.start.lon);
    struct C3D_RenderTarget top={0},bot={0};
    guess_ui_draw(&top,&bot);
    TEST(rendered);
    TEST(last_map_lat==ui.map_center.lat&&last_map_lon==ui.map_center.lon);
    TEST(map_tiles_get_coverage());
    pick_pin();press(KEY_A);TEST(ui.game.phase==GUESS_RESULT);
    guess_ui_draw(&top,&bot);press(KEY_A);
}
int main(void){
    /* Exercise the actual versioned record file; the emulator-independent
     * host path deliberately mimics sdmc:/ for a temporary test directory. */
    (void)mkdir("sdmc:",0700);
    (void)mkdir("sdmc:/3ds_google_maps",0700);
    remove("sdmc:/3ds_google_maps/streetguess_records.txt");
    remove("sdmc:/3ds_google_maps/streetguess_records.txt.tmp");
    /* Full UI + compact road pack: expect a road-derived candidate rather
     * than the fallback hub sampler, before Google imagery acceptance. */
    install_fake_road_pack();
    TEST(guess_ui_enter());TEST(ui.roads.file!=NULL);
    TEST(map_cancel_count==1);
    press(KEY_A);frame();frame();
    TEST(ui.game.phase==GUESS_PLAYING);
    TEST(query_lat>=40&&query_lat<=40.001);
    TEST(query_lon>=-74&&query_lon<=-73.999);
    guess_ui_leave();
    remove("sdmc:/3ds_google_maps/streetguess_roads.bin");
    finish_count=0;
    TEST(guess_ui_enter());TEST(guess_ui_active());
    /* NMPZ: the panorama must never rotate or move even if keys are held. */
    ui.game.rules=GUESS_NMPZ;
    press(KEY_A);TEST(ui.game.phase==GUESS_FINDING);
    frame();TEST(ui.game.phase==GUESS_PLAYING);
    float initial=ui.yaw;
    press(KEY_L|KEY_R|KEY_DLEFT|KEY_SELECT);
    TEST(ui.yaw==initial&&moves_count==0&&!ui.gyro);
    pick_pin();press(KEY_A);press(KEY_A);
    for(int r=1;r<GUESS_ROUNDS;r++)play_all();
    TEST(ui.game.phase==GUESS_COMPLETE&&ui.game.round==5);
    TEST(ui.games==1&&ui.game.total<=25000);
    FILE *records=fopen("sdmc:/3ds_google_maps/streetguess_records.txt","r");
    TEST(records!=NULL);
    unsigned ver=0,saved_games=0;
    TEST(fscanf(records,"%u %u",&ver,&saved_games)==2);
    fclose(records);
    TEST(ver==1&&saved_games==1);
    press(KEY_B);TEST(ui.game.phase==GUESS_MENU);
    guess_ui_leave();
    TEST(!guess_ui_active()&&scene_count==0&&finish_count==1);
    /* No Move: controls may rotate the camera, but cannot navigate. */
    TEST(guess_ui_enter());
    TEST(ui.games==1);
    ui.game.rules=GUESS_NO_MOVE;
    press(KEY_A);frame();TEST(ui.game.phase==GUESS_PLAYING);
    /* Opening a map does not guess or pause; B closes only the map. */
    TEST(!ui.map_focused);
    press(KEY_A);TEST(ui.map_focused && ui.game.phase==GUESS_PLAYING);
    press(KEY_A);TEST(!ui.game.has_pin && ui.game.phase==GUESS_PLAYING);
    press(KEY_B);TEST(!ui.map_focused && ui.game.phase==GUESS_PLAYING);
    initial=ui.yaw;
    press(KEY_L|KEY_R);
    TEST(moves_count==0&&ui.yaw!=initial);
    press(KEY_B);TEST(ui.game.phase==GUESS_PAUSED);
    press(KEY_A);TEST(ui.game.phase==GUESS_PLAYING);
    press(KEY_B);press(KEY_B);TEST(ui.game.phase==GUESS_MENU);
    guess_ui_leave();
    /* Normal Moving mode should actually call the shared navigation runtime. */
    TEST(guess_ui_enter());
    ui.game.rules=GUESS_MOVING;
    press(KEY_A);frame();TEST(ui.game.phase==GUESS_PLAYING);
    press(KEY_R);TEST(moves_count>0);
    uint32_t before_wait=ui.game.remaining_ms;network_busy=true;milliseconds+=5000;frame();
    TEST(ui.game.remaining_ms==before_wait);network_busy=false;frame();
    TEST(ui.game.remaining_ms+100>=before_wait);
    /* Return to Start is a distinct lower-screen action; D-Down must still
     * zoom the panorama out in Moving mode. */
    down=held=KEY_TOUCH;touch=(touchPosition){.px=45,.py=224};frame();
    TEST(return_count==1);
    down=held=0;frame();
    ui.fov=55;
    press(KEY_DDOWN);TEST(ui.fov==60&&return_count==1);
    guess_ui_leave();
    /* Failed official imagery access must not silently fall back to another provider. */
    TEST(guess_ui_enter());
    mock_denied=true;
    press(KEY_A);frame();
    TEST(ui.game.phase==GUESS_ERROR);
    TEST(!pano_runtime_has_live());
    press(KEY_B);guess_ui_leave();
    mock_denied=false;
    /* Bounded no-coverage retries must terminate instead of hanging forever. */
    TEST(guess_ui_enter());
    mock_no_coverage=true;
    press(KEY_A);
    for(int k=0;k<1000&&ui.game.phase==GUESS_FINDING;k++)frame();
    TEST(ui.game.phase==GUESS_ERROR);
    TEST(ui.game.attempt==GUESS_MAX_CANDIDATE_TRIES);
    guess_ui_leave();
    mock_no_coverage=false;
    /* Exact curated daily rounds ignore random-game replay avoidance. */
    TEST(guess_ui_enter());ui.mode=1;ui.difficulty=1;new_game();
    char daily_code[40];strcpy(daily_code,ui.challenge_code);
    for(unsigned r=0;r<5;r++){
        frame();frame();TEST(ui.game.phase==GUESS_PLAYING);
        TEST(!strcmp(current.id,guess_locations[ui.challenge.location[r]].id));
        guess_set_pin(&ui.game,ui.game.start);ui.map_focused=true;
        press(KEY_A);TEST(ui.game.phase==GUESS_RESULT);press(KEY_A);
    }
    TEST(ui.game.phase==GUESS_COMPLETE&&ui.game.total==25000&&ui.daily_best==25000);
    new_game();TEST(!strcmp(daily_code,ui.challenge_code));
    mock_no_coverage=true;frame();TEST(ui.game.phase==GUESS_ERROR&&ui.game.round==0&&ui.game.total==0);
    mock_no_coverage=false;press(KEY_A);frame();TEST(ui.game.phase==GUESS_PLAYING);
    guess_ui_leave();
    TEST(guess_ui_enter());ui.mode=3;ui.players=2;new_game();
    char party_code[40];strcpy(party_code,ui.challenge_code);
    for(unsigned r=0;r<5;r++){
        frame();frame();TEST(ui.game.phase==GUESS_PLAYING);
        guess_set_pin(&ui.game,ui.game.start);ui.map_focused=true;press(KEY_A);press(KEY_A);
    }
    TEST(ui.player_scores[0]==25000);press(KEY_A);
    TEST(ui.handoff&&ui.player==1);press(KEY_A);frame();
    TEST(ui.game.phase==GUESS_PLAYING&&!strcmp(party_code,ui.challenge_code));
    TEST(!strcmp(current.id,guess_locations[ui.challenge.location[0]].id));
    guess_ui_leave();
    TEST(guess_ui_enter());ui.mode=6;new_game();frame();frame();TEST(ui.game.phase==GUESS_PLAYING);
    strcpy(keyboard_answer,guess_locations[ui.challenge.location[0]].country);
    press(KEY_A);TEST(ui.game.phase==GUESS_RESULT&&ui.game.total==1);press(KEY_A);frame();
    strcpy(keyboard_answer,!strcmp(guess_locations[ui.challenge.location[1]].country,"FR")?"US":"FR");
    press(KEY_A);TEST(ui.game.phase==GUESS_RESULT&&ui.game.total==1);press(KEY_A);
    TEST(ui.game.phase==GUESS_COMPLETE);keyboard_answer[0]=0;guess_ui_leave();
    TEST(guess_ui_enter());ui.mode=5;new_game();
    for(unsigned r=0;r<5;r++)TEST(guess_locations[ui.challenge.location[r]].landmark);
    guess_ui_leave();
    /* Five correct countries continue into another batch with the score intact. */
    TEST(guess_ui_enter());ui.mode=6;new_game();
    for(unsigned r=0;r<5;r++){
        frame();frame();TEST(ui.game.phase==GUESS_PLAYING);
        strcpy(keyboard_answer,guess_locations[ui.challenge.location[r]].country);
        press(KEY_A);TEST(ui.game.phase==GUESS_RESULT);press(KEY_A);
    }
    TEST(ui.game.phase==GUESS_FINDING&&ui.game.round==0&&ui.game.total==5);
    keyboard_answer[0]=0;guess_ui_leave();
    remove(DAILY_FILE);
    remove("sdmc:/3ds_google_maps/streetguess_records.txt");
    remove("sdmc:/3ds_google_maps/streetguess_records.txt.tmp");
    (void)rmdir("sdmc:/3ds_google_maps");
    (void)rmdir("sdmc:");
    puts("StreetGuess UI: classic, daily five rounds, retry without penalty, daily records, identical pass-and-play rounds passed");
    return 0;
}