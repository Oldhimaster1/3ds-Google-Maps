#ifndef STREETGUESS_ROADS_H
#define STREETGUESS_ROADS_H
#include "guess_core.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
/* Optional, independent road-centerline geometry (never a Google panorama index).
 * Fixed-size records allow bounded-RAM random access on the original 3DS. */
typedef struct {
    FILE *file;
    uint32_t records;
} GuessRoads;
bool guess_roads_open(GuessRoads *roads, const char *path);
void guess_roads_close(GuessRoads *roads);
bool guess_roads_sample(GuessRoads *roads, uint64_t *rng,
                        GuessRegion region, GuessPoint *out);
#endif
