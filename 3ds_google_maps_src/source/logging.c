#include "logging.h"
#include <stdarg.h>
#include <stdio.h>
#include <3ds.h>

static FILE* log_file = NULL;
static const char* LOG_PATH = "sdmc:/3ds_google_maps_debug.log";

void log_init(void) {
    if (log_file) return;
    log_file = fopen(LOG_PATH, "w");
    if (!log_file) return;
    log_write("[LOG] === 3DS Google Maps debug log started ===\n");
}

void log_write(const char* fmt, ...) {
    if (!log_file) return;
    u64 ts = osGetTime();
    fprintf(log_file, "[%llu] ", (unsigned long long)ts);
    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);
    fflush(log_file);
}

void log_close(void) {
    if (log_file) {
        log_write("[LOG] === log ended ===\n");
        fclose(log_file);
        log_file = NULL;
    }
}
