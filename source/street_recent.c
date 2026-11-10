#include "street_recent.h"
#include <stdlib.h>
#include <string.h>
const StreetRecentEntry *street_recent_find(StreetRecent *cache, StreetProvider provider, const char *id) {
    if (!cache || provider != STREET_PROVIDER_PANORAMAX || !id || !*id) return NULL;
    for (unsigned i=0; i<STREET_RECENT_COUNT; i++) {
        StreetRecentEntry *slot=&cache->slots[i];
        if (slot->valid && slot->info.provider==provider && !strcmp(slot->info.id,id)) {
            slot->stamp=++cache->clock;
            return slot;
        }
    }
    return NULL;
}
bool street_recent_take(StreetRecent *cache, const PanoLiveImage *info, uint16_t *pixels) {
    if (!cache || !info || !pixels || info->provider!=STREET_PROVIDER_PANORAMAX || !info->id[0])
        return false;
    StreetRecentEntry *slot=NULL;
    for (unsigned i=0; i<STREET_RECENT_COUNT; i++) {
        StreetRecentEntry *candidate=&cache->slots[i];
        if (candidate->valid && candidate->info.provider==info->provider &&
            !strcmp(candidate->info.id,info->id)) { slot=candidate; break; }
    }
    if (!slot) {
        for (unsigned i=0; i<STREET_RECENT_COUNT; i++) {
            StreetRecentEntry *candidate=&cache->slots[i];
            if (!candidate->valid) { slot=candidate; break; }
            if (!slot || candidate->stamp<slot->stamp) slot=candidate;
        }
    }
    if (slot->pixels != pixels) free(slot->pixels);
    slot->info=*info;
    slot->pixels=pixels;
    slot->valid=true;
    slot->stamp=++cache->clock;
    return true;
}
void street_recent_clear(StreetRecent *cache) {
    if (!cache) return;
    for (unsigned i=0; i<STREET_RECENT_COUNT; i++) {
        free(cache->slots[i].pixels);
        memset(&cache->slots[i],0,sizeof(cache->slots[i]));
    }
    cache->clock=0;
}
