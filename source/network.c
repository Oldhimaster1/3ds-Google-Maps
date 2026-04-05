#include "network.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <curl/curl.h>

#define WEB_MERCATOR_HALF_WORLD 20037508.342789244

static bool network_initialized = false;
static bool tile_cache_dirs_ready = false;
static bool disk_cache_enabled = false;
static bool proxy_enabled = false;
static TileSource tile_source = TILE_SOURCE_SATELLITE;
static char g_proxy_base_url[256] = PROXY_BASE_URL;

static u32 last_tile_download_ms = 0;
static u32 last_tile_size_bytes = 0;
static bool last_tile_cache_hit = false;

/* =========================================================================
 * libcurl + mbedTLS HTTPS client
 *
 * libctru's httpc service uses the 3DS system SSL module which only supports
 * TLS 1.0 with old cipher suites. Modern HTTPS servers (OSM, Esri, …) reject
 * that handshake with error 0xD8A0A03C. We use libcurl with the mbedTLS
 * backend (official devkitPro portlibs packages) for HTTPS URLs over BSD
 * sockets from the SOC service.
 * ========================================================================= */

/* SOC buffer for BSD sockets (needed by libcurl) */
static u32  *g_soc_buf    = NULL;
static bool  g_curl_ready = false;

struct curl_buf {
    u8  *data;
    u32  size;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t realsize = size * nmemb;
    struct curl_buf *buf = (struct curl_buf *)userdata;
    u8 *tmp = (u8 *)realloc(buf->data, buf->size + realsize);
    if (!tmp) return 0;
    memcpy(tmp + buf->size, ptr, realsize);
    buf->data = tmp;
    buf->size += (u32)realsize;
    return realsize;
}

static void net_curl_init(void) {
    if (g_curl_ready) return;

    /* SOC service for BSD sockets — needed by libcurl */
    if (!g_soc_buf) {
        g_soc_buf = (u32 *)memalign(0x1000, 0x80000); /* 512 KB */
        if (g_soc_buf) {
            if (R_FAILED(socInit(g_soc_buf, 0x80000))) {
                /* Already initialised by another module — that's fine */
                free(g_soc_buf);
                g_soc_buf = NULL;
            }
        }
    }

    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        log_write("[NET] curl_global_init failed: %s\n", curl_easy_strerror(res));
        return;
    }
    g_curl_ready = true;
    log_write("[NET] curl init OK (%s)\n", curl_version());
}

static void net_curl_cleanup(void) {
    if (g_curl_ready) {
        curl_global_cleanup();
        g_curl_ready = false;
    }
    if (g_soc_buf) {
        socExit();
        free(g_soc_buf);
        g_soc_buf = NULL;
    }
}

/* Forward declarations for static helpers used by download_tile_with_curl */
static bool load_tile_from_disk(int tile_x, int tile_y, int zoom, u8** buffer, u32* size);
static void save_tile_to_disk(int tile_x, int tile_y, int zoom, const u8* buffer, u32 size);

/* Download any URL via libcurl — handles HTTPS, redirects, chunked, etc. */
static bool download_url_curl(const char *url, u8 **buffer, u32 *size) {
    if (!g_curl_ready) {
        net_curl_init();
        if (!g_curl_ready) return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        log_write("[CURL] easy_init failed\n");
        return false;
    }

    struct curl_buf buf = { NULL, 0 };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "3DS-Google-Maps/1.0 (Nintendo 3DS)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 16384L);

    log_write("[CURL] GET %s\n", url);
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        log_write("[CURL] failed: %s\n", curl_easy_strerror(res));
        free(buf.data);
        curl_easy_cleanup(curl);
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    log_write("[CURL] status=%ld bytes=%lu\n", http_code, (unsigned long)buf.size);

    curl_easy_cleanup(curl);

    if (http_code != 200 || !buf.data || buf.size == 0) {
        free(buf.data);
        return false;
    }

    *buffer = buf.data;
    *size = buf.size;
    return true;
}

/* -------------------------------------------------------------------------
 * Persistent-handle API for background download worker threads.
 *
 * Creating a new curl handle per tile means a full TLS handshake on every
 * request (~300-500 ms on 3DS).  Reusing the same handle across requests
 * lets libcurl keep the connection alive so only the *first* tile per thread
 * pays the handshake cost.  Subsequent tiles on the same connection take only
 * the actual transfer time (typically 100-300 ms instead of 3-5 s).
 * ------------------------------------------------------------------------- */
void *network_alloc_tile_curl(void) {
    if (!g_curl_ready) {
        net_curl_init();
        if (!g_curl_ready) return NULL;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        log_write("[NET] network_alloc_tile_curl: curl_easy_init failed\n");
        return NULL;
    }

    /* Set options that do not change between tile requests */
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "3DS-Google-Maps/1.0 (Nintendo 3DS)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,  1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,        5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,         15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,  10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,   0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,   0L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE,    16384L);
    /* Keep the TCP connection alive between requests */
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE,    1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE,    30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL,   10L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE,     0L);

    log_write("[NET] allocated persistent curl handle %p\n", (void *)curl);
    return (void *)curl;
}

void network_free_tile_curl(void *curl_handle) {
    if (curl_handle) {
        log_write("[NET] freeing persistent curl handle %p\n", curl_handle);
        curl_easy_cleanup((CURL *)curl_handle);
    }
}

bool download_tile_with_curl(int tile_x, int tile_y, int zoom, void *curl_handle,
                             u8 **buffer, u32 *size) {
    if (!network_initialized) return false;

    /* 1) SD card tile cache — instant, no network */
    if (load_tile_from_disk(tile_x, tile_y, zoom, buffer, size)) {
        last_tile_cache_hit  = true;
        last_tile_download_ms = 0;
        last_tile_size_bytes  = *size;
        return true;
    }

    /* 2) Build the tile URL */
    char url[512];
    build_osm_url(tile_x, tile_y, zoom, url, sizeof(url));

    last_tile_cache_hit = false;
    u64 t0 = osGetTime();
    bool ok = false;

    /* 3a) HTTPS — use the persistent handle to reuse the TLS connection */
    if (strncmp(url, "https://", 8) == 0 && curl_handle) {
        CURL *curl = (CURL *)curl_handle;
        struct curl_buf buf = { NULL, 0 };

        curl_easy_setopt(curl, CURLOPT_URL,           url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &buf);

        log_write("[CURL-P] GET %s\n", url);
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            log_write("[CURL-P] status=%ld bytes=%lu\n", http_code, (unsigned long)buf.size);
            if (http_code == 200 && buf.data && buf.size > 0) {
                *buffer = buf.data;
                *size   = buf.size;
                ok = true;
                buf.data = NULL;  /* ownership transferred */
            }
        } else {
            log_write("[CURL-P] failed: %s\n", curl_easy_strerror(res));
        }
        if (buf.data) free(buf.data);

    /* 3b) HTTP proxy — fall back to the per-request httpc path */
    } else {
        ok = download_url(url, buffer, size);
    }

    u64 t1 = osGetTime();
    last_tile_download_ms = (u32)(t1 - t0);
    last_tile_size_bytes  = ok ? *size : 0;

    if (ok) {
        save_tile_to_disk(tile_x, tile_y, zoom, *buffer, *size);
    }
    return ok;
}

bool network_has_proxy(void) {
    return proxy_enabled && g_proxy_base_url[0] != '\0';
}

bool network_proxy_available(void) {
    return g_proxy_base_url[0] != '\0';
}

void network_set_proxy_base_url(const char* url) {
    if (!url) { g_proxy_base_url[0] = '\0'; return; }
    snprintf(g_proxy_base_url, sizeof(g_proxy_base_url), "%s", url);
    // Trim trailing slash
    size_t len = strlen(g_proxy_base_url);
    while (len > 0 && g_proxy_base_url[len - 1] == '/') {
        g_proxy_base_url[--len] = '\0';
    }
}

const char* network_get_proxy_base_url(void) {
    return g_proxy_base_url;
}

bool network_get_proxy_enabled(void) {
    return proxy_enabled && network_proxy_available();
}

void network_set_proxy_enabled(bool enabled) {
    proxy_enabled = enabled && network_proxy_available();
}

TileSource network_get_tile_source(void) {
    return tile_source;
}

void network_set_tile_source(TileSource source) {
    if (source != TILE_SOURCE_SATELLITE) {
        tile_source = TILE_SOURCE_STREET;
        return;
    }
    tile_source = source;
}

const char* network_get_tile_source_name(void) {
    return (tile_source == TILE_SOURCE_SATELLITE) ? "Satellite" : "Street";
}

bool network_tile_requests_use_proxy(void) {
    return network_get_proxy_enabled();
}

const char* network_get_effective_tile_backend_name(void) {
    return network_tile_requests_use_proxy() ? "Proxy" : "Direct";
}

void network_set_disk_cache_enabled(bool enabled) {
    disk_cache_enabled = enabled;
}

bool network_get_disk_cache_enabled(void) {
    return disk_cache_enabled;
}

u32 network_get_last_tile_download_ms(void) {
    return last_tile_download_ms;
}

u32 network_get_last_tile_size_bytes(void) {
    return last_tile_size_bytes;
}

bool network_get_last_tile_was_cache_hit(void) {
    return last_tile_cache_hit;
}

#define TILE_CACHE_ROOT "sdmc:/3ds_google_maps/tiles"

static int remove_tree(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        // If it doesn't exist, treat as already cleared.
        if (errno == ENOENT) return 0;
        return -1;
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char child[512];
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            (void)remove_tree(child);
            (void)rmdir(child);
        } else {
            (void)unlink(child);
        }
    }

    closedir(dir);
    (void)rmdir(path);
    return 0;
}

bool network_clear_disk_tile_cache(void) {
    // Remove the entire cache directory; it will be recreated lazily on next save.
    int rc = remove_tree(TILE_CACHE_ROOT);
    tile_cache_dirs_ready = false;
    return rc == 0;
}

static void ensure_dir(const char* path) {
    if (!path || !path[0]) return;

    int rc = mkdir(path, 0777);
    if (rc == 0) return;
    if (errno == EEXIST) return;
}

static void build_tile_cache_path(int tile_x, int tile_y, int zoom, char* out_path, size_t out_size) {
    const char* source_dir = (tile_source == TILE_SOURCE_SATELLITE) ? "sat" : "street";
    // sdmc:/3ds_google_maps/tiles/<source>/<z>/<x>/<y>.png
    snprintf(out_path, out_size, TILE_CACHE_ROOT "/%s/%d/%d/%d.png", source_dir, zoom, tile_x, tile_y);
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

        char source_dir[96];
        snprintf(source_dir, sizeof(source_dir), TILE_CACHE_ROOT "/%s",
                 (tile_source == TILE_SOURCE_SATELLITE) ? "sat" : "street");
        ensure_dir(source_dir);

        // Ensure directories exist: sdmc:/3ds_google_maps/tiles/<z>/<x>/
        char z_dir[128];
        snprintf(z_dir, sizeof(z_dir), "%s/%d", source_dir, zoom);
        ensure_dir(z_dir);

        char zx_dir[160];
        snprintf(zx_dir, sizeof(zx_dir), "%s/%d/%d", source_dir, zoom, tile_x);
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

    // Initialise libcurl + mbedTLS for HTTPS downloads.
    net_curl_init();
    log_write("[NET] curl client init %s\n", g_curl_ready ? "OK" : "FAILED");

    // Load proxy URL from config file if present.
    // File format: one line containing the proxy base URL, e.g. http://192.168.1.118:8080
    {
        FILE* cfg = fopen("sdmc:/3ds_google_maps/proxy.cfg", "r");
        if (cfg) {
            char line[256] = {0};
            if (fgets(line, sizeof(line), cfg)) {
                // Trim trailing whitespace / newlines
                size_t len = strlen(line);
                while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' ||
                                   line[len-1] == ' '  || line[len-1] == '\t')) {
                    line[--len] = '\0';
                }
                if (len > 0) {
                    network_set_proxy_base_url(line);
                    proxy_enabled = true;
                    log_write("[NET] proxy.cfg loaded: %s\n", g_proxy_base_url);
                    printf("Proxy configured: %s\n", g_proxy_base_url);
                }
            }
            fclose(cfg);
        }
    }

    network_initialized = true;
    return true;
}

void network_cleanup(void) {
    if (!network_initialized) return;

    net_curl_cleanup();
    acExit();
    network_initialized = false;
}

bool download_url(const char* url, u8** buffer, u32* size) {
    if (!network_initialized || !url || !buffer || !size) {
        return false;
    }

    // Route HTTPS through libcurl + mbedTLS.
    // libctru's httpc SSL sysmodule only supports TLS 1.0 with old cipher
    // suites that modern servers reject (error 0xD8A0A03C). libcurl with
    // mbedTLS handles TLS 1.2 correctly.
    if (strncmp(url, "https://", 8) == 0) {
        return download_url_curl(url, buffer, size);
    }

    Result ret;
    httpcContext context;
    u32 statuscode = 0;
    u32 contentsize = 0, readsize = 0, downloaded_size = 0;
    u8 *buf = NULL, *lastbuf = NULL;

    // Work with a local copy of the URL so we can follow redirects.
    char cur_url[512];
    snprintf(cur_url, sizeof(cur_url), "%s", url);
    log_write("[DL] start url=%s\n", cur_url);

    int redirects = 0;
    while (redirects <= 3) {
        log_write("[DL] httpcOpenContext url=%s\n", cur_url);
        ret = httpcOpenContext(&context, HTTPC_METHOD_GET, cur_url, 0);
        if (R_FAILED(ret)) {
            log_write("[DL] httpcOpenContext FAILED ret=0x%08lX\n", ret);
            return false;
        }

        httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);

        ret = httpcAddRequestHeaderField(&context, "User-Agent",
                                        "3DS-Google-Maps/1.0 (Nintendo 3DS)");
        if (R_FAILED(ret)) { httpcCloseContext(&context); return false; }

        ret = httpcBeginRequest(&context);
        if (R_FAILED(ret)) {
            log_write("[DL] httpcBeginRequest FAILED ret=0x%08lX\n", ret);
            httpcCloseContext(&context); return false;
        }
        log_write("[DL] httpcBeginRequest OK, getting status...\n");

        ret = httpcGetResponseStatusCode(&context, &statuscode);
        if (R_FAILED(ret)) {
            log_write("[DL] httpcGetResponseStatusCode FAILED ret=0x%08lX\n", ret);
            httpcCloseContext(&context); return false;
        }
        log_write("[DL] HTTP status=%lu url=%s\n", statuscode, cur_url);

        if (statuscode == 301 || statuscode == 302 || statuscode == 303) {
            char location[512];
            ret = httpcGetResponseHeader(&context, "Location", location, sizeof(location));
            httpcCloseContext(&context);
            if (R_FAILED(ret) || location[0] == '\0') {
                log_write("[DL] redirect but no Location header, ret=0x%08lX\n", ret);
                return false;
            }
            log_write("[DL] redirect %d -> %s\n", redirects, location);
            snprintf(cur_url, sizeof(cur_url), "%s", location);
            redirects++;
            continue;
        }
        break;
    }

    if (statuscode != 200) {
        log_write("[DL] non-200 status=%lu, aborting\n", statuscode);
        httpcCloseContext(&context);
        return false;
    }

    // Get content length (may be 0 if not provided)
    ret = httpcGetDownloadSizeState(&context, NULL, &contentsize);
    if (R_FAILED(ret)) {
        httpcCloseContext(&context);
        return false;
    }

    // Start with a single page buffer
    buf = (u8*)malloc(0x1000);
    if (!buf) {
        httpcCloseContext(&context);
        return false;
    }

    // Download data in chunks with a 15-second wall-clock timeout.
    // httpc has no built-in connect/read timeout on 3DS hardware, so a
    // stalled remote server would block the download thread forever.
    u64 dl_start = osGetTime();
    do {
        if (osGetTime() - dl_start > 15000) {
            free(buf);
            httpcCloseContext(&context);
            return false;
        }
        ret = httpcDownloadData(&context, buf + downloaded_size, 0x1000, &readsize);
        downloaded_size += readsize;

        if (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING) {
            lastbuf = buf;
            buf = (u8*)realloc(buf, downloaded_size + 0x1000);
            if (!buf) {
                free(lastbuf);
                httpcCloseContext(&context);
                return false;
            }
        }
    } while (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);

    if (R_FAILED(ret)) {
        log_write("[DL] download loop FAILED ret=0x%08lX downloaded=%lu\n", ret, downloaded_size);
        free(buf);
        httpcCloseContext(&context);
        return false;
    }
    log_write("[DL] download complete bytes=%lu url=%s\n", downloaded_size, cur_url);

    // Resize buffer to final size
    if (downloaded_size > 0) {
        lastbuf = buf;
        buf = (u8*)realloc(buf, downloaded_size);
        if (!buf) {
            buf = lastbuf;
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

    // 1) Try SD card cache first
    if (load_tile_from_disk(tile_x, tile_y, zoom, buffer, size)) {
        last_tile_cache_hit = true;
        last_tile_download_ms = 0;
        last_tile_size_bytes = *size;
        return true;
    }

    // 2) Download from network and populate cache
    last_tile_cache_hit = false;
    u64 t0 = osGetTime();
    bool ok = download_url(url, buffer, size);
    u64 t1 = osGetTime();
    last_tile_download_ms = (u32)(t1 - t0);
    last_tile_size_bytes = ok ? *size : 0;
    if (ok) {
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
    if (tile_source == TILE_SOURCE_SATELLITE) {
        if (network_get_proxy_enabled()) {
            snprintf(url, url_size,
                     "%s/sat/%d/%d/%d.png",
                     g_proxy_base_url, zoom, tile_x, tile_y);
        } else {
            // Use the Esri tile cache XYZ endpoint (CDN-served, simpler URL, different
            // TLS profile than the export API). Esri's tile cache uses z/y/x ordering.
            snprintf(url, url_size,
                     "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%d/%d/%d",
                     zoom, tile_y, tile_x);
        }
    } else if (network_get_proxy_enabled()) {
        snprintf(url, url_size,
                 "%s/%d/%d/%d.png",
                 g_proxy_base_url, zoom, tile_x, tile_y);
    } else {
        // Direct OSM raster tile URL. This keeps the core map usable without a proxy.
        snprintf(url, url_size,
                 TILE_BASE_URL "/%d/%d/%d.png",
                 zoom, tile_x, tile_y);
    }
}

void build_google_url(int tile_x, int tile_y, int zoom, char* url, size_t url_size) {
    // Google Maps tile server URL format (requires API key for production use)
    // Note: This is just an example format - actual Google Maps API requires authentication
    snprintf(url, url_size, 
             "https://mt1.google.com/vt/lyrs=m&x=%d&y=%d&z=%d",
             tile_x, tile_y, zoom);
}
