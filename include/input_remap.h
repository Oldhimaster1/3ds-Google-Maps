#ifndef INPUT_REMAP_H
#define INPUT_REMAP_H
#include <stdbool.h>
#include <stdint.h>
#define INPUT_REMAP_FILE "sdmc:/3ds_google_maps/control_map.cfg"
#define INPUT_REMAP_PROFILES 4u
#define INPUT_REMAP_BUTTONS 11u
typedef enum { REMAP_MAP=0, REMAP_STREET=1, REMAP_EARTH=2, REMAP_GUESS=3 } RemapProfile;
/* Index 0..10 identifies logical A,B,X,Y,L,R,Up,Down,Left,Right,Select. */
typedef struct { uint8_t physical[INPUT_REMAP_PROFILES][INPUT_REMAP_BUTTONS]; } InputRemap;
void input_remap_default(InputRemap *m);
bool input_remap_valid(const InputRemap *m);
bool input_remap_load(InputRemap *m,const char *path);
bool input_remap_save(const InputRemap *m,const char *path);
bool input_remap_bind(InputRemap *m,unsigned profile,unsigned action,unsigned physical);
uint32_t input_remap_keys(const InputRemap *m,unsigned profile,uint32_t raw_keys);
int input_remap_pressed(uint32_t raw_down);
/* Shared read-only mapping used by Maps, StreetGuess and modal screens. */
void input_remap_set_active(const InputRemap *m);
uint32_t input_remap_apply(unsigned profile,uint32_t raw_keys);
const char *input_remap_button_name(unsigned button);
const char *input_remap_profile_name(unsigned profile);
#endif
