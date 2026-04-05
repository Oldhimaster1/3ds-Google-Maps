#ifndef NETWORK_H
#define NETWORK_H

#include <3ds.h>
#include <stdbool.h>

#define TILE_BASE_URL "https://tile.openstreetmap.org"
#define PROXY_BASE_URL "http://192.168.1.118:8080"

typedef enum {
	TILE_SOURCE_STREET = 0,
	TILE_SOURCE_SATELLITE = 1
} TileSource;

// Network initialization and cleanup
bool network_init(void);
void network_cleanup(void);
bool network_has_proxy(void);
bool network_proxy_available(void);
bool network_get_proxy_enabled(void);
void network_set_proxy_enabled(bool enabled);
void network_set_proxy_base_url(const char* url);
const char* network_get_proxy_base_url(void);
TileSource network_get_tile_source(void);
void network_set_tile_source(TileSource source);
const char* network_get_tile_source_name(void);
bool network_tile_requests_use_proxy(void);
const char* network_get_effective_tile_backend_name(void);

// Map tile downloading
bool download_tile(int tile_x, int tile_y, int zoom, u8** buffer, u32* size);
bool is_connected(void);

// Persistent curl handle for download worker threads.
void *network_alloc_tile_curl(void);
void  network_free_tile_curl(void *curl_handle);
bool  download_tile_with_curl(int tile_x, int tile_y, int zoom, void *curl_handle, u8 **buffer, u32 *size);

// SD tile cache toggle
void network_set_disk_cache_enabled(bool enabled);
bool network_get_disk_cache_enabled(void);

// Deletes all files under sdmc:/3ds_google_maps/tiles (persistent tile cache).
// Returns true if the cache was cleared (or didn't exist).
bool network_clear_disk_tile_cache(void);

// Last tile fetch metrics (for on-screen status).
// If the last tile was served from SD cache, download_ms will be 0.
u32 network_get_last_tile_download_ms(void);
u32 network_get_last_tile_size_bytes(void);
bool network_get_last_tile_was_cache_hit(void);

// Generic HTTP downloading (for proxy endpoints like /geocode)
bool download_url(const char* url, u8** buffer, u32* size);

// URL building for different map providers
void build_osm_url(int tile_x, int tile_y, int zoom, char* url, size_t url_size);
void build_google_url(int tile_x, int tile_y, int zoom, char* url, size_t url_size);

#endif // NETWORK_H
