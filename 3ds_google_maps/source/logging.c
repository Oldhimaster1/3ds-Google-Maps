#include "logging.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static FILE* log_file = NULL;
static const char* LOG_PATH = "sdmc:/3ds_google_maps.log";

void log_init(void) {
    if (log_file) return;  // Already initialized
    
    log_file = fopen(LOG_PATH, "w");
    if (!log_file) {
        printf("Warning: Could not open log file at %s\n", LOG_PATH);
        return;
    }
    
    log_write("=== 3DS Google Maps Log Started ===\n");
}

void log_write(const char* fmt, ...) {
    if (!log_file) return;
    
    va_list args;
    va_start(args, fmt);

    // Write to console
    vprintf(fmt, args);
    va_end(args);

    // Write to log file
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);

    fflush(log_file);
}

void log_close(void) {
    if (log_file) {
        log_write("\n=== 3DS Google Maps Log Ended ===\n");
        fclose(log_file);
        log_file = NULL;
    }
}
