#ifndef MAP_REGION_H
#define MAP_REGION_H

#include "tilepack.h"
#include <stdio.h>

#define REGION_LIMIT 4096
#define REGION_NAME_LIMIT 24
#define REGION_PATH_LIMIT 128
#define REGION_DIR "sdmc:/3ds_google_maps/regions"

typedef struct {
    FILE *file;
    TilePackEntry *entries;
    unsigned count;
    uint32_t data_offset;
} RegionJob;

bool region_open(RegionJob *job, const char *path, const TilePackEntry *plan, unsigned count);
bool region_append(RegionJob *job, unsigned index, const void *bytes, unsigned size);
bool region_finish(RegionJob *job, const char *part, const char *final);
void region_close(RegionJob *job);
typedef struct { double lat, lon; } RegionPoint;
unsigned region_plan(TilePackEntry *tiles, unsigned capacity, int min_zoom, int max_zoom,
                     const RegionPoint *path, unsigned points,
                     int reference_zoom, int padding);
#endif
