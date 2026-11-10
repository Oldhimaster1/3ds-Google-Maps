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
/* Mapillary client tokens contain only printable, URL-escaped ASCII. */
bool street_mapillary_token_valid(const char *token);
#endif
