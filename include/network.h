#ifndef NETWORK_H
#define NETWORK_H
#include <stdatomic.h>

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

typedef struct {
    unsigned tiles, min_zoom, max_zoom;
    unsigned x, y, zoom;
    long bytes;
} OfflinePackInfo;

#define REGION_PACK_LIMIT 12
unsigned network_region_count(void);
bool network_install_region(const char *path, TileSource source);
bool network_remove_region(const char *path);
bool network_pack_covers(int x, int y, int z);
typedef struct {
    long http_status;
    unsigned retry_after_ms;
    int curl_code;
} NetworkRequestInfo;

bool network_request(void *handle, const char *url, size_t limit, atomic_bool *cancel,
                     u8 **buffer, u32 *size);
bool network_request_ex(void *handle, const char *url, size_t limit, atomic_bool *cancel,
                        u8 **buffer, u32 *size, NetworkRequestInfo *info);
bool network_region_endpoint(TileSource source, char *endpoint, size_t size);
bool network_region_url(const char *endpoint, int x, int y, int z, char *url, size_t size);

bool network_get_offline_pack(TileSource source, OfflinePackInfo *info);
bool network_remove_offline_pack(TileSource source);

// Map tile downloading
bool download_tile(int tile_x, int tile_y, int zoom, u8** buffer, u32* size);
bool is_connected(void);
bool network_socket_service_ready(void);

// Persistent curl handle for download worker threads.
// Allocate one handle per thread; reusing it across requests keeps the
// TLS connection alive so only the first tile per thread pays handshake cost.
void *network_alloc_tile_curl(void);
void  network_free_tile_curl(void *curl_handle);
typedef struct {
    unsigned sd_ms, network_ms, decode_ms, upload_ms;
    bool local;
} TileTiming;

bool fetch_map_tile(int x, int y, int zoom, void *curl, bool online, TileSource source, u8 **buffer, u32 *size, bool *offline_hit, TileTiming *timing);
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
