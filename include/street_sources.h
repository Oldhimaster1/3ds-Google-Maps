#ifndef STREET_SOURCES_H
#define STREET_SOURCES_H
#include <stdbool.h>
#include <stddef.h>
#include "panorama_live.h"

const char *street_provider_name(StreetProvider provider);
bool street_source_url(StreetProvider provider, double lat, double lon,
                       double radius_deg, const char *mapillary_token,
                       char *url, size_t capacity);
unsigned street_parse_candidates(StreetProvider provider,
                       const char *json, size_t len, double lat, double lon,
                       PanoLiveImage *out, unsigned capacity);
bool street_bing_face_url(const char *id, unsigned face, char *url, size_t cap);
bool street_google_find_id(const char *json, size_t len, const char *id,
                           PanoLiveImage *out);
/* Mapillary client tokens contain only printable, URL-escaped ASCII. */
bool street_mapillary_token_valid(const char *token);
#endif
