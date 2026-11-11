#ifndef GEO_FORMATS_H
#define GEO_FORMATS_H
#include <stdbool.h>
typedef enum { GEO_INVALID=0, GEO_DECIMAL, GEO_DMS, GEO_PLUS_CODE } GeoFormat;
GeoFormat geo_parse_coordinates(const char *text, double *latitude, double *longitude);
#endif
