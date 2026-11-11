#ifndef DETAIL_HOST_NET_H
#define DETAIL_HOST_NET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <3ds.h>
bool is_connected(void);
void *network_alloc_tile_curl(void);
void network_free_tile_curl(void *h);
bool network_request(void *h,const char *url,size_t limit,atomic_bool *cancel,
                     u8 **bytes,u32 *length);
#endif
