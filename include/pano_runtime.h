#ifndef PANO_RUNTIME_H
#define PANO_RUNTIME_H

#include <stdbool.h>
#include "panorama_live.h"

/* All public functions run on the main UI thread. HTTP and JPEG decoding use
 * a background worker; GPU uploads only happen from pano_runtime_poll(). */
bool pano_runtime_start(double lat, double lon);
bool pano_runtime_step(int direction);
void pano_runtime_poll(void);
void pano_runtime_stop(void);
bool pano_runtime_busy(void);
bool pano_runtime_has_live(void);
const char *pano_runtime_status(void);
const PanoLiveImage *pano_runtime_current(void);
#endif
