#ifndef DETAIL_HOST_3DS_H
#define DETAIL_HOST_3DS_H
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct MockThread *Thread;
#define U64_MAX UINT64_MAX
Thread threadCreate(void (*fn)(void*),void *arg,uint32_t stack,int priority,
                    int processor_id,bool detached);
void threadJoin(Thread thread,u64 timeout);
void threadFree(Thread thread);
u64 osGetTime(void);
#endif
