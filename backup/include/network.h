#ifndef NETWORK_H
#define NETWORK_H

#include <3ds.h>
#include <stdbool.h>

// Network initialization and cleanup
bool network_init(void);
void network_cleanup(void);

// Map tile downloading
bool download_tile(int tile_x, int tile_y, int zoom, u8** buffer, u32* size);
bool is_connected(void);

// SD tile cache toggle
void network_set_disk_cache_enabled(bool enabled);
bool network_get_disk_cache_enabled(void);

// Generic HTTP downloading (for proxy endpoints like /geocode)
bool download_url(const char* url, u8** buffer, u32* size);

// URL building for different map providers
void build_osm_url(int tile_x, int tile_y, int zoom, char* url, size_t url_size);
void build_google_url(int tile_x, int tile_y, int zoom, char* url, size_t url_size);

#endif // NETWORK_H
