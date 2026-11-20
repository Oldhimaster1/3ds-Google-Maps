#ifndef MAPS_AMBIENT_MUSIC_H
#define MAPS_AMBIENT_MUSIC_H
#include <stdbool.h>
void ambient_music_update(bool active);
void ambient_music_exit(void);
typedef enum {AMBIENT_READY,AMBIENT_DSP_ERROR,AMBIENT_MEMORY_ERROR} AmbientStatus;
AmbientStatus ambient_music_status(void);
void ambient_music_set_volume(unsigned percent);
/* Retry only on an explicit user action, never every frame. */
void ambient_music_retry(void);
#endif
