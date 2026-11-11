#ifndef STUB_PANO_RUNTIME_H
#define STUB_PANO_RUNTIME_H
#include "citro3d.h"
#include <stdbool.h>
#include "panorama_live.h"
void pano_runtime_stop(void);void pano_runtime_end_game_google(void);bool pano_runtime_start_game_google(double,double);bool pano_runtime_jump_game_google(const char*,double,double);void pano_runtime_poll(void);bool pano_runtime_has_live(void);bool pano_runtime_network_active(void);const char*pano_runtime_status(void);const PanoLiveImage*pano_runtime_current(void);bool pano_runtime_step_at(int,float);

bool pano_runtime_start_curated(const char*,const char*);
#endif