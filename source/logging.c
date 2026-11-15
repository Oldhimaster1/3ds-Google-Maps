#include "logging.h"
#include <stdarg.h>
#include <stdio.h>
#include <3ds.h>
#include <3ds/synchronization.h>
#include <errno.h>

static FILE* log_file = NULL;
static LightLock log_lock;
static bool log_lock_ready = false;
static const char* LOG_PATH = "sdmc:/3ds_google_maps_debug.log";

void log_init(void) {
    if (log_file) return;
    LightLock_Init(&log_lock);
    log_lock_ready = true;
    log_file = fopen(LOG_PATH, "w");
    if (!log_file) {
        printf("[LOG] failed to open %s (errno=%d)\n", LOG_PATH, errno);
        return;
    }
    log_write("[LOG] === 3DS Google Maps debug log started ===\n");
    log_write("[LOG] path=%s\n", LOG_PATH);
}

void log_write(const char* fmt, ...) {
    if (!log_file || !fmt) return;
    if (log_lock_ready) LightLock_Lock(&log_lock);
    u64 ts = osGetTime();
    fprintf(log_file, "[%llu] ", (unsigned long long)ts);
    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);
    fflush(log_file);
    if (log_lock_ready) LightLock_Unlock(&log_lock);
}

void log_close(void) {
    if (log_file) {
        log_write("[LOG] === log ended ===\n");
        fclose(log_file);
        log_file = NULL;
    }
    log_lock_ready = false;
}
