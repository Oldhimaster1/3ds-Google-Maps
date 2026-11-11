#ifndef STUB_PANO_GYRO_H
#define STUB_PANO_GYRO_H
#include <stdbool.h>
typedef struct {int ready;} PanoGyroFilter;
void pano_gyro_reset(PanoGyroFilter*);bool pano_gyro_step(PanoGyroFilter*,float,float,float,float,bool,float*,float*);

#endif