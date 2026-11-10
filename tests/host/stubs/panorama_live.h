#ifndef STUB_PANORAMA_LIVE_H
#define STUB_PANORAMA_LIVE_H
#include <stdbool.h>
typedef enum {STREET_PROVIDER_AUTO,STREET_PROVIDER_PANORAMAX,STREET_PROVIDER_MAPILLARY,STREET_PROVIDER_KARTAVIEW,STREET_PROVIDER_GOOGLE,STREET_PROVIDER_BING} StreetProvider;
typedef struct {StreetProvider provider;bool is_pano;double lat,lon;float azimuth;char id[48];char credit[96];} PanoLiveImage;

#endif