#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>

// Initialize logging to SD card
void log_init(void);

// Write a formatted message to the log file
void log_write(const char* fmt, ...);

// Close the log file
void log_close(void);

#endif // LOGGING_H
