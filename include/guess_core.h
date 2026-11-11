#ifndef STREETGUESS_CORE_H
#define STREETGUESS_CORE_H
#include <stdbool.h>
#include <stdint.h>
#define GUESS_ROUNDS 5
#define GUESS_HISTORY 48
#define GUESS_MAX_CANDIDATE_TRIES 24

typedef enum { GUESS_WORLD, GUESS_USA, GUESS_EUROPE, GUESS_MAP_COUNT } GuessRegion;
typedef enum { GUESS_MOVING, GUESS_NO_MOVE, GUESS_NMPZ, GUESS_RULE_COUNT } GuessRules;
typedef enum { GUESS_MENU, GUESS_FINDING, GUESS_PLAYING, GUESS_RESULT, GUESS_COMPLETE, GUESS_PAUSED, GUESS_ERROR } GuessPhase;
typedef struct { double lat, lon; } GuessPoint;
typedef struct {
    GuessPhase phase, resume;
    GuessRegion region;
    GuessRules rules;
    unsigned sampler; /* 0 balanced, 1 urban, 2 rural, 3 broad */
    uint64_t rng, last_tick;
    uint32_t remaining_ms, time_limit_ms;
    unsigned round, attempt, history_size, history_pos;
    GuessPoint start, pin, history[GUESS_HISTORY];
    bool has_pin;
    unsigned score[GUESS_ROUNDS];
    double distances_km[GUESS_ROUNDS];
    unsigned total;
} GuessGame;
void guess_init(GuessGame *g, uint64_t seed);
void guess_begin(GuessGame *g, GuessRegion region, GuessRules rules, unsigned time_limit_seconds);
bool guess_random_candidate(GuessGame *g, GuessPoint *out);
bool guess_candidate_acceptable(const GuessGame *g, GuessPoint point);
bool guess_accept_start(GuessGame *g, GuessPoint point, uint64_t now_ms);
void guess_set_pin(GuessGame *g, GuessPoint pin);
bool guess_submit(GuessGame *g);
bool guess_advance(GuessGame *g);
void guess_tick(GuessGame *g, uint64_t now_ms);
void guess_toggle_pause(GuessGame *g, uint64_t now_ms);
void guess_error(GuessGame *g);
double guess_distance_km(GuessPoint a, GuessPoint b);
unsigned guess_score_km(double distance, GuessRegion region);
/* Independent Mercator coordinates for the player's guessing map. */
void guess_map_center_delta(GuessPoint *center, int zoom, double dx, double dy);
GuessPoint guess_map_at_pixel(GuessPoint center, int zoom, double px, double py,
                              double width, double height);
void guess_map_project(GuessPoint center, GuessPoint point, int zoom,
                       double width, double height, float *x, float *y);
#endif