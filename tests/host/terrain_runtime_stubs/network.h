#ifndef TERRAIN_MOCK_NETWORK_H
#define TERRAIN_MOCK_NETWORK_H
#include <3ds.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
bool is_connected(void);
void *network_alloc_tile_curl(void);
void network_free_tile_curl(void *handle);
bool network_request(void *handle,const char *url,size_t limit,
                     atomic_bool *cancel,u8 **buffer,u32 *size);
#endif
