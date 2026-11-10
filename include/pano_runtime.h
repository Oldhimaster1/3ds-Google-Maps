#ifndef PANO_RUNTIME_H
#define PANO_RUNTIME_H
#include <stdbool.h>
#include "panorama_live.h"

/* HTTP / image decode run on a worker; GPU swap runs in poll on the UI thread. */
bool pano_runtime_start(double lat, double lon);
bool pano_runtime_step(int direction);
void pano_runtime_poll(void);
void pano_runtime_stop(void);
bool pano_runtime_busy(void);
bool pano_runtime_has_live(void);
const char *pano_runtime_status(void);
const PanoLiveImage *pano_runtime_current(void);

/* Provider preference and map-marker toggle are saved on SD (never token). */
void pano_runtime_set_provider(StreetProvider p);
StreetProvider pano_runtime_provider(void);
void pano_runtime_set_coverage(bool enabled);
bool pano_runtime_coverage_enabled(void);
unsigned pano_runtime_coverage(PanoLiveImage *items, unsigned max_items);
unsigned pano_runtime_coverage_count(void);
const PanoLiveImage *pano_runtime_coverage_at(unsigned i);
#endif
