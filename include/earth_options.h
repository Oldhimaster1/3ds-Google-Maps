#ifndef EARTH_OPTIONS_H
#define EARTH_OPTIONS_H
#include <stdbool.h>
#define EARTH_OPTIONS_FILE "sdmc:/3ds_google_maps/earth_options.cfg"
typedef struct {
  unsigned input_speed;       /* 0 slow, 1 normal, 2 fast */
  unsigned dead_zone;         /* 10..60 raw circle pad */
  unsigned panorama_quality;  /* 0 fast, 1 normal, 2 detail, 3 automatic */
  unsigned sampler;           /* 0 balanced, 1 urban, 2 rural, 3 broad */
  unsigned terrain_exaggeration; /* 0..4: 1x, 1.5x, 2x, 3x, 4x */
  unsigned buildings;         /* 0 off, 1 on */
  unsigned globe_detail;      /* 0 low, 1 auto, 2 high */
  unsigned gyro_invert;       /* 0 or 1 */
} EarthOptions;
void earth_options_default(EarthOptions *p);
void earth_options_clamp(EarthOptions *p);
bool earth_options_load(EarthOptions *p,const char *path);
bool earth_options_save(const EarthOptions *p,const char *path);
float earth_options_terrain_scale(const EarthOptions *p);
float earth_options_stick_scale(const EarthOptions *p);
#endif
