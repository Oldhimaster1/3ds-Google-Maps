#ifndef LOGGING_H
#define LOGGING_H

void log_init(void);
void log_write(const char* fmt, ...);
void log_close(void);

#endif
