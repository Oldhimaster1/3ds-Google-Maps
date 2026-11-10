#ifndef PANORAMA_LIVE_H
#define PANORAMA_LIVE_H

#include <stdbool.h>
#include <stddef.h>

#define PANO_NEARBY_MAX 16
#define PANO_API_BASE "https://api.panoramax.xyz/api"

typedef enum {
    STREET_PROVIDER_AUTO = 0,
    STREET_PROVIDER_PANORAMAX = 1,
    STREET_PROVIDER_MAPILLARY = 2,
    STREET_PROVIDER_KARTAVIEW = 3,
    STREET_PROVIDER_GOOGLE = 4,
    STREET_PROVIDER_BING = 5,
    STREET_PROVIDER_COUNT = 6
} StreetProvider;

typedef struct {
    StreetProvider provider;
    char id[48];
    char sequence[64];
    char image_url[1024];
    char preview_url[1024];
    char next_id[40];
    char prev_id[40];
    char credit[96];
    char license[40];
    double lat, lon;
    float azimuth;
    bool is_pano; /* Never wrap an ordinary street photo on a 360 sphere. */
} PanoLiveImage;

/* Search is limited to a square around the displayed map's coordinates. */
bool pano_search_url(double lat, double lon, double radius_degrees,
                     char *out, size_t capacity);
bool pano_image_url(const char *id, char *out, size_t capacity);
bool pano_metadata_url(const char *id, char *out, size_t capacity);

/* Only full 360 degree pictures are accepted; normal photos would distort. */
unsigned pano_parse_nearby(const char *json, size_t length,
                           double lat, double lon,
                           PanoLiveImage *items, unsigned capacity);
bool pano_parse_image(const char *json, size_t length, PanoLiveImage *out);

/* Calibrated camera movement. The positive/negative sign may be toggled in UI. */
void pano_apply_gyro(float raw_pitch, float raw_yaw, float dps_per_raw,
                     float dt, bool invert, float *pitch, float *yaw);
#endif
