#ifndef TERRAIN_MOCK_3DS_H
#define TERRAIN_MOCK_3DS_H
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef struct MockThread *Thread;
#define U64_MAX UINT64_MAX
Thread threadCreate(void (*func)(void *),void *arg,uint32_t stack,int priority,int core,bool detached);
void threadJoin(Thread t,uint64_t timeout);
void threadFree(Thread t);
#endif
