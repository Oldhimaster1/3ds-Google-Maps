#ifndef GUESS_CHALLENGE_H
#define GUESS_CHALLENGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "guess_core.h"
typedef struct { const char *id,*country,*name,*hint,*license,*credit,*metadata_url; double lat,lon; } GuessLocation;
extern const GuessLocation guess_locations[];
extern const unsigned guess_location_count;
extern const uint32_t guess_dataset_id;
typedef struct {uint32_t seed;unsigned difficulty;unsigned location[GUESS_ROUNDS];} GuessChallenge;
bool guess_challenge_make(GuessChallenge *c,uint32_t seed,unsigned difficulty);
bool guess_challenge_code(const GuessChallenge *c,char *out,size_t cap);
bool guess_challenge_parse(GuessChallenge *c,const char *text);
/* Days since Unix epoch, UTC. Caller must ensure the console clock is correct. */
uint32_t guess_challenge_day(int64_t unix_seconds);
#endif
