#ifndef STREET_RECENT_H
#define STREET_RECENT_H
#include "panorama_live.h"
#include <stdint.h>
#define STREET_RECENT_COUNT 2
typedef struct {
    bool valid;
    PanoLiveImage info;
    uint16_t *pixels;
    unsigned long stamp;
} StreetRecentEntry;
typedef struct {
    StreetRecentEntry slots[STREET_RECENT_COUNT];
    unsigned long clock;
} StreetRecent;
/* In-session RAM history for Panoramax only; caller transfers ownership on true. */
bool street_recent_take(StreetRecent *cache, const PanoLiveImage *info, uint16_t *pixels);
const StreetRecentEntry *street_recent_find(StreetRecent *cache, StreetProvider provider, const char *id);
void street_recent_clear(StreetRecent *cache);
#endif
