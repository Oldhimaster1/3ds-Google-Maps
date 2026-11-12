/* POSIX shim for testing the actual GPS HTTPS server, not console emulation. */
#ifndef GPS_HOST_3DS_H
#define GPS_HOST_3DS_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int Result;
#define R_FAILED(r) ((r) < 0)
#define U64_MAX UINT64_MAX
typedef struct { pthread_t id; void (*entry)(void *); void *arg; } *Thread;
static inline u64 osGetTime(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static inline void svcSleepThread(int64_t ns) {
    struct timespec t = { ns / 1000000000, ns % 1000000000 };
    nanosleep(&t, NULL);
}
static inline void *host_thread_entry(void *arg) {
    Thread t = arg; t->entry(t->arg); return NULL;
}
static inline Thread threadCreate(void (*entry)(void *), void *arg,
                                  size_t stack, int prio, int core, bool detached) {
    (void)stack; (void)prio; (void)core; (void)detached;
    Thread t = malloc(sizeof(*t));
    if (!t) return NULL;
    t->entry = entry; t->arg = arg;
    if (pthread_create(&t->id, NULL, host_thread_entry, t)) { free(t); return NULL; }
    return t;
}
static inline void threadJoin(Thread t, u64 timeout) {
    (void)timeout; pthread_join(t->id, NULL);
}
static inline void threadFree(Thread t) { free(t); }
static inline Result socInit(u32 *buf, size_t size) { (void)buf; (void)size; return 0; }
static inline void socExit(void) {}
#endif
