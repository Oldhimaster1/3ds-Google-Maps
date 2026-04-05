#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static bool network_initialized = false;
static bool tile_cache_dirs_ready = false;
static bool disk_cache_enabled = true;

void network_set_disk_cache_enabled(bool enabled) {
    disk_cache_enabled = enabled;
}

bool network_get_disk_cache_enabled(void) {
    return disk_cache_enabled;
}

#define TILE_CACHE_ROOT "sdmc:/3ds_google_maps/tiles"

static void ensure_dir(const char* path) {
    if (!path || !path[0]) return;

    int rc = mkdir(path, 0777);
    if (rc == 0) return;
    if (errno == EEXIST) return;
}

static void build_tile_cache_path(int tile_x, int tile_y, int zoom, char* out_path, size_t out_size) {
    // sdmc:/3ds_google_maps/tiles/<z>/<x>/<y>.png
    snprintf(out_path, out_size, TILE_CACHE_ROOT "/%d/%d/%d.png", zoom, tile_x, tile_y);
}

static bool load_tile_from_disk(int tile_x, int tile_y, int zoom, u8** buffer, u32* size) {
    if (!buffer || !size) return false;
    if (!disk_cache_enabled) return false;

    char path[256];
    build_tile_cache_path(tile_x, tile_y, zoom, path, sizeof(path));

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long file_size = ftell(f);
    if (file_size <= 0 || file_size > (1024 * 1024)) { // sanity cap at 1MB
        fclose(f);
        return false;
    }
    rewind(f);

    u8* buf = (u8*)malloc((size_t)file_size);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t read = fread(buf, 1, (size_t)file_size, f);
    fclose(f);
    if (read != (size_t)file_size) {
        free(buf);
        return false;
    }

    *buffer = buf;
    *size = (u32)file_size;
    return true;
}

static void save_tile_to_disk(int tile_x, int tile_y, int zoom, const u8* buffer, u32 size) {
    if (!buffer || size == 0) return;
    if (!disk_cache_enabled) return;

    // Skip disk work if the file already exists.
    char path[256];
    build_tile_cache_path(tile_x, tile_y, zoom, path, sizeof(path));
    FILE* existing = fopen(path, "rb");
    if (existing) {
        fclose(existing);
        return;
    }

    // Try writing immediately; if it fails because dirs don't exist, create them once and retry.
    FILE* f = fopen(path, "wb");
    if (!f) {
        // Ensure base directories exist only once.
        if (!tile_cache_dirs_ready) {
            ensure_dir("sdmc:/3ds_google_maps");
            ensure_dir("sdmc:/3ds_google_maps/tiles");
            tile_cache_dirs_ready = true;
        }

        // Ensure directories exist: sdmc:/3ds_google_maps/tiles/<z>/<x>/
        char z_dir[128];
        snprintf(z_dir, sizeof(z_dir), TILE_CACHE_ROOT "/%d", zoom);
        ensure_dir(z_dir);

        char zx_dir[160];
        snprintf(zx_dir, sizeof(zx_dir), TILE_CACHE_ROOT "/%d/%d", zoom, tile_x);
        ensure_dir(zx_dir);

        f = fopen(path, "wb");
        if (!f) return;
    }

    // Use a larger stdio buffer to reduce SD write overhead.
    static char file_buf[16 * 1024];
    setvbuf(f, file_buf, _IOFBF, sizeof(file_buf));
    fwrite(buffer, 1, (size_t)size, f);
    fclose(f);
}

bool network_init(void) {
    if (network_initialized) return true;
    
    Result ret;
    
    // Initialize AC (wireless) service
    ret = acInit();
    if (R_FAILED(ret)) {
        printf("Failed to initialize AC service: 0x%08lX\n", ret);
        return false;
    }
    
    // Wait for internet connection
    bool connected = false;
    for (int i = 0; i < 10; i++) {
        u32 wifi_status = 0;
        ret = ACU_GetWifiStatus(&wifi_status);
        if (R_SUCCEEDED(ret) && wifi_status) {
            connected = true;
            break;
        }
        svcSleepThread(1000000000LL); // Sleep for 1 second
    }
    
    if (!connected) {
        printf("No WiFi connection detected!\n");
        printf("Please ensure WiFi is enabled and connected.\n");
        acExit();
        return false;
    }
    
    printf("WiFi connected successfully!\n");
    network_initialized = true;
    return true;
}

void network_cleanup(void) {
    if (!network_initialized) return;

    acExit();
    network_initialized = false;
}

bool download_url(const char* url, u8** buffer, u32* size) {
    if (!network_initialized || !url || !buffer || !size) {
        return false;
    }

    Result ret;
    httpcContext context;
    u32 statuscode = 0;
    u32 contentsize = 0, readsize = 0, downloaded_size = 0;
    u8 *buf = NULL, *lastbuf = NULL;

    // Initialize HTTP context
    ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(ret)) {
        printf("Failed to open HTTP context: 0x%08lX\n", ret);
        return false;
    }
    
    // Set headers for HTTP request
    ret = httpcAddRequestHeaderField(&context, "User-Agent", 
                                   "3DS-Google-Maps/1.0 (Nintendo 3DS)");
    if (R_FAILED(ret)) {
        printf("Failed to set User-Agent: 0x%08lX\n", ret);
        httpcCloseContext(&context);
        return false;
    }
    
    // Begin the request
    ret = httpcBeginRequest(&context);
    if (R_FAILED(ret)) {
        printf("Failed to begin HTTP request: 0x%08lX\n", ret);
        httpcCloseContext(&context);
        return false;
    }
    
    // Get response status
    ret = httpcGetResponseStatusCode(&context, &statuscode);
    if (R_FAILED(ret)) {
        printf("Failed to get response status: 0x%08lX\n", ret);
        httpcCloseContext(&context);
        return false;
    }
    
    if (statuscode != 200) {
        printf("HTTP request failed with status: %lu\n", statuscode);
        httpcCloseContext(&context);
        return false;
    }
    
    // Get content length (may be 0 if not provided)
    ret = httpcGetDownloadSizeState(&context, NULL, &contentsize);
    if (R_FAILED(ret)) {
        printf("Failed to get content length: 0x%08lX\n", ret);
        httpcCloseContext(&context);
        return false;
    }

    // Optional debug
    // printf("Expected content size: %lu bytes\n", contentsize);
    
    // Start with a single page buffer
    buf = (u8*)malloc(0x1000);
    if (!buf) {
        printf("Failed to allocate initial buffer\n");
        httpcCloseContext(&context);
        return false;
    }
    
    // Download data in chunks, resizing buffer as needed
    do {
        ret = httpcDownloadData(&context, buf + downloaded_size, 0x1000, &readsize);
        downloaded_size += readsize;
        
        if (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING) {
            lastbuf = buf;
            buf = (u8*)realloc(buf, downloaded_size + 0x1000);
            if (!buf) {
                printf("Failed to reallocate buffer\n");
                free(lastbuf);
                httpcCloseContext(&context);
                return false;
            }
        }
    } while (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);
    
    if (R_FAILED(ret)) {
        printf("Failed to download tile data: 0x%08lX\n", ret);
        free(buf);
        httpcCloseContext(&context);
        return false;
    }
    
    // Resize buffer to final size
    if (downloaded_size > 0) {
        lastbuf = buf;
        buf = (u8*)realloc(buf, downloaded_size);
        if (!buf) {
            buf = lastbuf; // Keep the original buffer if realloc fails
        }
    }
    
    *buffer = buf;
    *size = downloaded_size;
    httpcCloseContext(&context);

    return true;
}

bool download_tile(int tile_x, int tile_y, int zoom, u8** buffer, u32* size) {
    if (!network_initialized) {
        return false;
    }

    char url[512];
    build_osm_url(tile_x, tile_y, zoom, url, sizeof(url));
    printf("Downloading tile: %d/%d/%d\n", zoom, tile_x, tile_y);

    // 1) Try SD card cache first
    if (load_tile_from_disk(tile_x, tile_y, zoom, buffer, size)) {
        printf("Loaded tile from SD cache (%lu bytes)\n", (unsigned long)(*size));
        return true;
    }

    // 2) Download from network and populate cache
    bool ok = download_url(url, buffer, size);
    if (ok) {
        printf("Successfully downloaded tile (%lu bytes)\n", (unsigned long)(*size));
        save_tile_to_disk(tile_x, tile_y, zoom, *buffer, *size);
    }
    return ok;
}

bool is_connected(void) {
    if (!network_initialized) return false;
    
    u32 wifi_status = 0;
    Result ret = ACU_GetWifiStatus(&wifi_status);
    return R_SUCCEEDED(ret) && wifi_status;
}

void build_osm_url(int tile_x, int tile_y, int zoom, char* url, size_t url_size) {
    // Oracle Cloud proxy.
    // Format: http://<host>:<port>/{z}/{x}/{y}.png
    snprintf(url, url_size,
             "http://193.122.144.54:8080/%d/%d/%d.png",
             zoom, tile_x, tile_y);
}

void build_google_url(int tile_x, int tile_y, int zoom, char* url, size_t url_size) {
    // Google Maps tile server URL format (requires API key for production use)
    // Note: This is just an example format - actual Google Maps API requires authentication
    snprintf(url, url_size, 
             "https://mt1.google.com/vt/lyrs=m&x=%d&y=%d&z=%d",
             tile_x, tile_y, zoom);
}
