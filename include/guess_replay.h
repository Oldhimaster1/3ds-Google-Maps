#ifndef GUESS_REPLAY_H
#define GUESS_REPLAY_H
#include <stdbool.h>
#include <stdint.h>
#define GUESS_REPLAY_MAX 128
#define GUESS_REPLAY_FILE "sdmc:/3ds_google_maps/streetguess_recent_cells.dat"
typedef struct {int16_t lat_q,lon_q;} GuessReplayCell;
typedef struct {
    GuessReplayCell cells[GUESS_REPLAY_MAX];
    uint32_t cursor,count;
} GuessReplay;
void guess_replay_clear(GuessReplay *h);
bool guess_replay_can_play(const GuessReplay *h,double lat,double lon,unsigned radius_cells);
void guess_replay_add(GuessReplay *h,double lat,double lon);
bool guess_replay_load(GuessReplay *h,const char *path);
bool guess_replay_save(const GuessReplay *h,const char *path);
#endif
