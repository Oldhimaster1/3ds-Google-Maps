#ifndef MAPS_INTL_H
#define MAPS_INTL_H
#include <stdbool.h>
#include <stddef.h>
#define INTL_LANGUAGE_COUNT 9
#define INTL_TEXT_CAP 4096
const char *intl_language_name(unsigned language);
unsigned intl_language(void);
void intl_set_language(unsigned language);
const char *intl_translate(const char *english);
bool intl_display(const char *english,char *out,size_t capacity);
/* UI-thread only. Logical UTF-8 stays unchanged in storage and requests. */
int intl_snprintf(char *out,size_t capacity,const char *format,...);
bool intl_visual(const char *logical, char *visual, size_t capacity);
bool intl_load(const char *path);
bool intl_save(const char *path);
unsigned intl_music_volume(void);
void intl_set_music_volume(unsigned percent);
bool intl_music(void);
void intl_set_music(bool enabled);
#endif
