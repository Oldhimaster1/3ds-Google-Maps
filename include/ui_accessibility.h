#ifndef UI_ACCESSIBILITY_H
#define UI_ACCESSIBILITY_H
#include <stdbool.h>
#define UI_ACCESS_CFG "sdmc:/3ds_google_maps/ui_access.cfg"
typedef struct {
    unsigned size;          /* 0 Normal, 1 Large, 2 Extra Large */
    unsigned weight;        /* 0 Regular, 1 Medium, 2 Bold */
    bool high_contrast;
} UiAccessConfig;
const UiAccessConfig *ui_access_get(void);
void ui_access_set(unsigned size, unsigned weight, bool high_contrast);
void ui_access_reset(void);
float ui_access_scale(void);
/* Use user-supplied paths in host tests, UI_ACCESS_CFG on the 3DS. */
bool ui_access_load(const char *path);
bool ui_access_save(const char *path);
#endif