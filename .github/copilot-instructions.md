# 3DS Google Maps — Copilot / AI Assistant Instructions

This document is the comprehensive knowledge base for AI-assisted development on this project. Read it fully before making any changes. It covers the entire architecture, every non-obvious decision, all problems that were already solved (do NOT re-solve them), and how everything fits together.

---

## Project Overview

Nintendo 3DS homebrew app that shows an interactive OpenStreetMap-based map on the dual screens, with real-time GPS tracking via a phone browser, route planning, place search, favorites, and a full settings UI.

**Language**: C (C99)
**Build system**: devkitARM + devkitPro Makefile conventions
**Libraries**: libctru, citro2d, citro3d, libcurl (mbedTLS backend), mbedTLS, libpng, stb_image

---

## Repository Layout

```
source/                  ← All .c files (all auto-compiled by SOURCES := source in Makefile)
  main.c                 ← EVERYTHING UI-related lives here
  network.c              ← All tile downloading, curl, disk cache
  map_tiles.c            ← Tile coordinate math, RAM cache, decode pipeline, rendering
  gps.c                  ← GPS mode dispatch and fix storage
  gps_server.c           ← HTTPS mbedTLS GPS bridge server
  logging.c              ← SD card file logger
  qrcode.c               ← Self-contained QR encoder (no deps)
  simple_png.c           ← PNG loader wrapper (outputs Morton-tiled ABGR directly)
  stb_image.c            ← stb_image implementation (#define STB_IMAGE_IMPLEMENTATION + #include stb_image_all.h)
include/                 ← All headers
  network.h
  map_tiles.h
  gps.h
  gps_server.h
  gps_server_cert.h      ← EMBEDDED TLS CERT AND KEY — do not regenerate unless expired
  qrcode.h
  logging.h
  simple_png.h
  stb_image.h
  stb_image_all.h        ← Real stb_image v2.30 (7100 lines, full JPEG+PNG decoder)
  stb_image_impl.h
tools/
  tile_proxy.py          ← Local debug proxy; NOT required at runtime
  generate_cert.py       ← Generated the embedded cert (don't need to run again)
Makefile
```

---

## Build System Details

### Toolchain
- devkitARM (ARM cross-compiler targeting 3DS/ARM11)
- devkitPro's MSYS2 environment on Windows OR native Linux/macOS environment
- `DEVKITPRO` must be set (e.g. `/opt/devkitpro` on Linux, `C:\devkitPro` on Windows = `/c/devkitPro` in MSYS2)

### Required devkitPro packages
```
3ds-dev           (libctru, citro2d, citro3d, devkitARM)
3ds-curl          (libcurl 8.4.0 with mbedTLS backend for 3DS)
3ds-mbedtls       (mbedTLS 2.28.8 for 3DS)
3ds-libpng        (libpng for 3DS)
```
Install with: `dkp-pacman -S 3ds-curl 3ds-mbedtls 3ds-libpng`

### Makefile key settings
```makefile
SOURCES   := source
INCLUDES  := include
LIBS      := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lpng -lz -lcitro2d -lcitro3d -lctru -lm
LIBDIRS   := $(PORTLIBS) $(CTRULIB)
```
`$(PORTLIBS)` expands to devkitPro's portlibs path which contains 3ds-curl, 3ds-mbedtls, 3ds-libpng.
Do NOT add custom thirdparty paths — all deps are in portlibs.

### Build command sequence (in devkitPro MSYS2 shell)
```bash
export DEVKITPRO=/c/devkitPro
export DEVKITARM=/c/devkitPro/devkitARM
export PATH=/c/devkitPro/devkitARM/bin:/c/devkitPro/tools/bin:$PATH
cd /c/Users/.../3ds_google_maps_src
make
```
Output: `3ds_google_maps.3dsx` (place in `/3ds/3ds_google_maps/` on SD card)

---

## TLS / Networking Architecture — READ THIS CAREFULLY

### Why libcurl + mbedTLS (not OpenSSL, not httpc)
The 3DS's built-in httpc service only supports HTTP or TLS 1.0. OpenStreetMap (`tile.openstreetmap.org`) requires TLS 1.2+, so httpc fails with TLS errors.

**OpenSSL 3.x was tried and abandoned.** The OpenSSL DRBG/PRNG system requires entropy sources that don't exist on 3DS, causing the seeding to hang or fail at startup. Do not try to use OpenSSL — it doesn't work.

**mbedTLS is the correct solution** and is in the official devkitPro package repo as `3ds-mbedtls`. The mbedTLS entropy on 3DS uses `MBEDTLS_ENTROPY_HARDWARE_ALT` which is wired to `sslcGenerateRandomData()` — a 3DS system service. This requires `sslcInit(0)` to be called at app start (it is, in `main.c`).

**libcurl** is in `3ds-curl` and uses mbedTLS as its backend. It handles all HTTPS tile downloads cleanly.

### Tile downloads (`network.c`)
- `network_init()` → calls `net_curl_init()` → calls `socInit()` and `curl_global_init()`
- `download_url(url, buf, maxlen)` → routes to `download_url_curl()` if url starts with "https://", else `download_url_httpc()` for plain HTTP
- `download_url_curl()` → uses `curl_easy_*` API, `CURLOPT_SSL_VERIFYPEER = 0` (no cert store on 3DS)
- Tile disk cache: `sdmc:/3ds_google_maps/tiles/{z}/{x}/{y}.png`
- Tile RAM cache: array of 64 `TileCache` entries, evicted LRU-style

### GPS HTTPS server (`gps_server.c`)
- mbedTLS TLS 1.2 server on port 8443
- Serves a static HTML page at `GET /` — the page uses `navigator.geolocation.watchPosition()` and POSTs JSON to `POST /location`
- Receives `{"lat": 35.123, "lon": -120.456}` and calls `gps_inject_fix(lat, lon)`
- Uses `gps_server_cert.h` embedded cert/key — self-signed RSA-2048, valid 2026-2036
- Background thread (`gps_server_thread`) handles one connection at a time

### CRITICAL: socInit is called once only
`socInit()` is a 3DS system call that allocates the socket SOC service. It can only be called once per app session. `network.c` calls it first inside `net_curl_init()`. The GPS server (`gps_server.c`) also calls `socInit()` — but if it's already initialized, the call returns a failure code. **The fix**: if `socInit()` fails in `gps_server_start()`, just free the allocated SOC buffer and continue — don't return false. The sockets work fine regardless.

```c
// In gps_server_start(): CORRECT handling
u32 *soc_buf = (u32*)memalign(0x1000, SOC_BUFFERSIZE);
Result socResult = socInit(soc_buf, SOC_BUFFERSIZE);
if (R_FAILED(socResult)) {
    // Already initialized by network module — this is OK, just free our buffer
    free(soc_buf);
    soc_buf = NULL;
    log_write("[GPS-SVR] socInit failed (already init'd by network) — continuing\n");
    // Do NOT return false here!
}
```

---

## Screen Layout

### Top screen (400×240)
- Normally shows the map (citro2d rendered tiles + overlays)
- While `g_screen_mode == APP_SCREEN_SETTINGS` AND `g_settings_tab == SETTINGS_TAB_GPS` AND `g_qr_valid == true`: shows the QR code instead of the map
- Other overlays drawn on top of map: GPS position marker (yellow circle), route polyline (blue), night mode tint (dark gray semi-transparent), debug info text

### Bottom screen (320×240)
- While `g_screen_mode == APP_SCREEN_MAIN`: shows `render_bottom_panel()` — action buttons row + status bar
- While `g_screen_mode == APP_SCREEN_SETTINGS`: shows `render_settings_panel()` — full settings UI

### `g_screen_mode` values
```c
APP_SCREEN_MAIN      // normal map+bottom panel
APP_SCREEN_SETTINGS  // settings panel open
```

---

## main.c Architecture (This File Controls Everything)

This is the largest and most important file. It contains:

### Global state variables (key ones)
```c
float g_map_lat, g_map_lon;       // center of map view
int g_zoom;                        // zoom level (1-18)
bool g_tracking;                   // GPS auto-follow mode
bool g_night_mode;
int g_tile_source;                 // TILE_OSM or TILE_ESRI_SAT

AppScreenMode g_screen_mode;
SettingsTab g_settings_tab;        // VIEW, TILES, DATA, GPS

// QR code state
uint8_t g_qr_modules[QR_MAX_MODULES * QR_MAX_MODULES];
int g_qr_size;
bool g_qr_valid;

// Input state
touchPosition g_touch_prev, g_touch_cur;
bool g_is_dragging;
```

### Main loop structure
```c
while (aptMainLoop()) {
    hidScanInput();
    handle_input();         // touch, buttons → map pan/zoom, button taps
    update_gps();           // if g_tracking, center map on GPS fix
    
    // Render top screen
    C2D_SceneBegin(g_render_top);
    render_map();           // or render_qr_top_screen() if QR is active
    
    // Render bottom screen  
    C2D_SceneBegin(g_render_bottom);
    if (g_screen_mode == APP_SCREEN_SETTINGS)
        render_settings_panel();
    else
        render_bottom_panel();
    
    C3D_FrameEnd(0);
}
```

### Settings panel tabs
- `SETTINGS_TAB_VIEW` — night mode, debug overlay, crosshair, zoom reset
- `SETTINGS_TAB_TILES` — tile source (OSM street / Esri satellite), clear cache, prefetch ring size
- `SETTINGS_TAB_DATA` — route start/end, search box, Nominatim geocode, favorites
- `SETTINGS_TAB_GPS` — GPS mode (NMEA Client / HTTPS Server) + Start/Stop button + shows IP/URL + QR

### Settings hit detection
```c
SettingsHitTarget get_settings_hit(touchPosition tp);  // hit tests → returns enum
void apply_settings_hit(SettingsHitTarget hit);        // executes the action
bool is_recent_settings_hit(SettingsHitTarget hit);    // true if tapped within 180ms (for button highlight)
```
All `SettingsHitTarget` enum values are named `SETTINGS_HIT_*`.

### GPS tab in settings — key behavior
When `SETTINGS_HIT_GPS_MODE_SERVER` is tapped:
1. `gps_cleanup()` — stops whatever GPS was running
2. `gps_set_source_mode(GPS_SOURCE_HTTPS_SERVER)`
3. `gps_init()` — starts the server (calls `gps_server_start(8443)` internally)
4. Generates QR: `qr_encode(url, len, g_qr_modules, &g_qr_size)`, sets `g_qr_valid = true`
5. The top screen now shows the QR code

Also: lazy QR generation in `render_settings_panel()` — if GPS tab is open, server is running, but `g_qr_valid` is false, regenerate QR.

### IP display in GPS settings card
Uses `gethostid()` (from `<unistd.h>`) to get the 3DS's WiFi IP as a 32-bit int, then formats it:
```c
u32 ip = (u32)gethostid();
snprintf(ipbuf, sizeof(ipbuf), "%lu.%lu.%lu.%lu",
    (ip) & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
```
This is shown in the GPS settings card even before the server starts.

---

## GPS System (`gps.c` + `gps_server.c`)

### GPS modes
```c
typedef enum {
    GPS_SOURCE_NMEA_CLIENT  = 0,  // TCP client connecting to phone's GPS2IP app
    GPS_SOURCE_HTTPS_SERVER = 1,  // 3DS is the server, phone browser sends location
} GpsSourceMode;
```

### Public API (`gps.h`)
```c
void gps_set_source_mode(GpsSourceMode mode);
GpsSourceMode gps_get_source_mode(void);
bool gps_init(void);                    // starts client or server
void gps_cleanup(void);                 // stops everything
bool gps_is_connected(void);            // server mode: gps_server_is_running()
bool gps_has_fix(void);                 // true if a valid lat/lon is available
void gps_get_fix(double *lat, double *lon);
void gps_inject_fix(double lat, double lon);  // called by GPS server thread
```

### GPS server flow (`gps_server.c`)
1. `gps_server_start(8443)`:
   - Optionally calls `socInit()` (skips gracefully if already done)
   - Gets local IP via `gethostid()`
   - Seeds `mbedtls_ctr_drbg` using `sslcGenerateRandomData()` entropy
   - Parses DER cert and RSA key from `gps_server_cert.h`
   - `mbedtls_ssl_config_defaults(MBEDTLS_SSL_SRV_CONF, ...)`
   - Creates TCP socket, binds to port 8443, listens
   - Spawns background thread: `threadCreate(gps_server_thread, ...)`
2. `gps_server_thread` loop:
   - `accept()` → new connection
   - `mbedtls_ssl_handshake()`
   - Read HTTP request headers
   - If `GET /`: send HTML page with Geolocation JS
   - If `POST /location`: read body, parse JSON `{"lat":..., "lon":...}`, call `gps_inject_fix()`
   - Send HTTP 200 response, close connection, loop back to accept
3. `gps_server_stop()`: sets `g_server_running = false`, closes socket, joins thread

### The embedded cert (`include/gps_server_cert.h`)
```c
extern const unsigned char gps_server_cert_der[];  // DER format, ~781 bytes
extern const unsigned int  gps_server_cert_der_len;
extern const unsigned char gps_server_key_der[];   // RSA-2048 PKCS#1 DER, ~1191 bytes
extern const unsigned int  gps_server_key_der_len;
```
Self-signed, CN=3ds-gps-server, valid 2026-02-01 to 2036-01-30.
Do not regenerate unless it expires. The cert is what gets embedded in the QR code URL's HTTPS origin.

---

## QR Code System (`qrcode.c` + `qrcode.h`)

### Why custom QR encoder
No QR library exists in the 3DS portlibs, and adding a dependency is complex. The encoder is ~600 lines of pure C with no external dependencies.

### Capabilities
- Byte mode only (no alphanumeric/kanji shortcut, byte mode works for all ASCII URLs)
- EC level L (lowest error correction, maximizes data capacity)
- Versions 1-4:
  - V1: 21×21 modules, up to 17 bytes
  - V2: 25×25 modules, up to 32 bytes
  - V3: 29×29 modules, up to 53 bytes
  - V4: 33×33 modules, up to 78 bytes
- A typical GPS server URL `https://192.168.1.100:8443` is 28 bytes → fits in V2

### API
```c
// qrcode.h
#define QR_MAX_MODULES 33   // version 4 module count

// Returns QR version (1-4) or 0 on failure
// modules must be at least QR_MAX_MODULES * QR_MAX_MODULES bytes
// *out_size receives the actual module count for this version (21, 25, 29, or 33)
int qr_encode(const char *text, int text_len,
              uint8_t *modules, int *out_size);
```

### How QR is displayed (in `main.c`)
```c
void render_qr_top_screen(void) {
    // Draw white background
    // Calculate pixel size: each module = floor(240 / g_qr_size) pixels
    // Draw 4-module quiet zone around all sides
    // For each module: draw filled rectangle,
    //   dark module (1) → black, light module (0) → white
}
```
The QR is centered on the top screen. Module size is calculated to fit in 240px height with the quiet zone.

---

## Tile / Map System (`map_tiles.c` + `network.c`)

### Coordinate math
- Web Mercator (EPSG:3857), zoom levels 0-18
- `lat_lon_to_tile(lat, lon, zoom, *tx, *ty)` — converts lat/lon to tile X/Y integers
- `tile_to_lat_lon(tx, ty, zoom, *lat, *lon)` — inverse
- `pixels_to_lat_lon_delta(px, py, zoom, *dlat, *dlon)` — pixel offset to coordinate delta (used for panning)

### Tile sources (`TileSource` enum)
```c
TILE_OSM       // https://tile.openstreetmap.org/{z}/{x}/{y}.png
TILE_ESRI_SAT  // https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}
```

### RAM tile cache
- 64 slots (`CachedTile tile_cache[MAX_CACHED_TILES]`) in `map_tiles.c`
- Each entry: `{int tile_x, tile_y, zoom; C2D_Image image; bool valid; bool pending; u32 last_used;}`
- `find_cached_tile(x,y,z)` → check RAM cache → if missing, `enqueue_tile_download()` → worker thread downloads + decodes → `map_tiles_process_downloads()` on main thread uploads to GPU

### Decode pipeline (PERFORMANCE CRITICAL — DO NOT REVERT)
JPEG decode on Old 3DS ARM11 (268 MHz, no NEON) takes ~1–2 seconds. To keep the UI responsive, the heavy CPU work runs on worker threads:

1. **Worker thread** (`download_worker`):
   - Downloads raw bytes via `download_tile_with_curl()`
   - Detects JPEG (`FF D8`) or PNG from magic bytes
   - **JPEG**: `stbi_load_from_memory()` → linear RGBA → `rgba_linear_to_3ds_tiled()` → Morton-tiled ABGR
   - **PNG**: `decode_png()` → Morton-tiled ABGR (libpng, single pass)
   - Pushes `decoded_pixels` buffer into `dl_res_q`
2. **Main thread** (`map_tiles_process_downloads()`):
   - Calls `create_image_from_decoded(decoded_pixels)` — only `C3D_TexInit` + `C3D_TexUpload` (~10ms)
   - Frees `decoded_pixels` after upload

`DLResult` carries `decoded_pixels` (Morton-tiled ABGR, 256×256×4 bytes), NOT raw `buffer`/`size`.

**DO NOT move JPEG/PNG decode back to the main thread** — it causes 1–2 second UI freezes per satellite tile.

### Disk tile cache
- Path: `sdmc:/3ds_google_maps/tiles/{zoom}/{x}/{y}.png`
- Written by `download_tile_with_curl()` after successful download
- Read from disk on cache miss; decoded by the worker thread (same pipeline as live downloads)

### Prefetch ring
- After rendering visible tiles, prefetch 1 ring of tiles around the viewport
- `g_prefetch_ring_size` (0 = disabled, 1 = immediate neighbors, 2 = wider ring)

---

## Logging System (`logging.c` + `logging.h`)

```c
void log_write(const char *fmt, ...);  // printf-style, writes to sdmc:/3ds_google_maps/log.txt
```
Log file is opened in `"w"` mode at app start (overwriting previous session). Every key operation logs its result. **When debugging on-device, always pull this file first.**

Log prefixes used throughout the codebase:
- `[MAIN]` — startup, init, shutdown
- `[NET]` — curl, httpc, tile downloads
- `[TILES]` — cache hits/misses, downloads
- `[GPS-SVR]` — all GPS server events (very verbose!)
- `[GPS]` — GPS mode dispatch, inject_fix
- `[SETTINGS]` — UI taps in settings panel
- `[QR]` — QR encoding events

---

## Settings UI — Full Control Flow

### Entering settings
- Tap the "Menu" button in `render_bottom_panel()` → sets `g_screen_mode = APP_SCREEN_SETTINGS`
- Opens on `g_settings_tab = SETTINGS_TAB_VIEW` by default (or last open tab)

### Tab bar on bottom screen
- Four buttons at the top of the settings panel: View | Tiles | Data | GPS
- Tapping switches `g_settings_tab`, re-renders panel content

### View tab controls
- Night Mode toggle
- Debug Overlay toggle
- Crosshair toggle
- "Reset Zoom" button (sets `g_zoom = 10`)

### Tiles tab controls
- "OSM Street" / "Satellite" radio buttons → sets `g_tile_source`, clears RAM tile cache
- "Clear Disk Cache" button → deletes `sdmc:/3ds_google_maps/tiles/` (confirmation not required)
- Prefetch ring stepper (+/-) → `g_prefetch_ring_size`

### Data tab controls
- Start/End text fields for route (software keyboard input via `swkbdInputText`)
- "Route" button → queries OSRM API for route, draws polyline
- "Search" field → queries Nominatim API, snaps map to result
- "Identify" button → reverse geocodes map center, shows result in status bar
- Favorites list (up to 10) — "Save" adds current position, tapping one jumps to it

### GPS tab controls
- "NMEA Client" button → sets mode to GPS_SOURCE_NMEA_CLIENT, shows IP/port entry
- "Server" button → sets mode to GPS_SOURCE_HTTPS_SERVER, starts server, shows URL + QR
- "Start/Stop" button → toggles `gps_init()` / `gps_cleanup()`
- IP/URL display — shows `https://<IP>:8443` using `gethostid()` even before server starts
- QR code → displayed on top screen while this tab is open and server is running

### Exiting settings
- Tap "Close" button in settings → `g_screen_mode = APP_SCREEN_MAIN`
- Press B button → `g_screen_mode = APP_SCREEN_MAIN`

---

## Known Issues and Constraints

### socInit double-init (SOLVED — DO NOT RE-FIX)
`socInit()` can only be called once per 3DS session. `network.c` calls it in `net_curl_init()`. `gps_server.c` also calls it. The GPS server handles this by checking the return value and continuing if it failed (meaning it was already initialized).

### OpenSSL doesn't work on 3DS (DO NOT TRY AGAIN)
OpenSSL 3.x DRBG fails to seed on 3DS. Use mbedTLS for all crypto. It's in `3ds-mbedtls` in devkitPro.

### `MBEDTLS_ERR_NET_SEND_FAILED` undefined (SOLVED)
mbedTLS net_sockets module is not included in the 3DS port. Use numeric error codes (e.g. `-1`, `-0x004E`) instead of the named constants from `mbedtls/net_sockets.h`. Do not include `mbedtls/net_sockets.h`.

### TLS 1.0 rejection
The built-in 3DS httpc service sends TLS 1.0 which is rejected by modern tile servers. Solution is libcurl (already implemented). Do not route HTTPS tile downloads through httpc.

### O3DS performance
On the original 3DS (old 3DS), TLS handshakes are ~300-500ms. On New 3DS they're much faster. Tile downloads at zoom 14+ take 2-6 seconds each on O3DS.

### QR size limits
The GPS URL `https://xxx.xxx.xxx.xxx:8443` is always ≤28 chars → V2 QR (25×25 modules). The QR library supports up to V4 (78 bytes). If we ever need a longer URL, it still fits in V4 as long as it's under 78 bytes.

### Software keyboard
3DS software keyboard (`swkbd*` API) is modal and blocks the main thread while open. This is expected behavior.

---

## How To Add New Features

### Adding a new settings control
1. Add a `SETTINGS_HIT_MY_NEW_THING` value to the `SettingsHitTarget` enum
2. Draw the button/control in the appropriate tab render block in `render_settings_panel()`
3. Add a hit-test case in `get_settings_hit()`
4. Add a dispatch case in `apply_settings_hit()`
5. Optional: add button-press highlight feedback using `is_recent_settings_hit()`

### Adding a new tile source
1. Add value to `TileSource` enum in `network.h`
2. Add URL template in `build_tile_url()` in `network.c`
3. Add button/label in Tiles settings tab render + hit test + apply

### Adding a new GPS source mode
1. Add value to `GpsSourceMode` enum in `gps.h`
2. Add init/cleanup/connected/hasfix logic to `gps.c` switch statements
3. Add appropriate source file (e.g. `gps_bt.c` for Bluetooth)
4. Add settings tab UI for configuring the new mode

### Adding a new API call (geocoding, routing, etc.)
1. Build the HTTPS URL
2. Call `download_url(url, buf, buflen)` — it handles curl/https automatically
3. Parse the JSON response manually (no JSON library is used — just `strstr`/`sscanf` style parsing)
4. Update map state and re-render

---

## devkitPro 3DS API Cheat Sheet

```c
// System init (main.c does these at startup)
romfsInit();
gfxInitDefault();
C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
C2D_Prepare();
httpcInit(0);
sslcInit(0);         // required for mbedTLS entropy
ptmuInit();
acInit();

// Screen rendering
C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
C2D_SceneBegin(renderTarget);
C2D_TargetClear(renderTarget, clearColor);
// ... draw stuff ...
C3D_FrameEnd(0);

// Touch input
hidScanInput();
touchPosition tp; hidTouchRead(&tp);
// tp.px, tp.py — pixel coords on bottom screen

// Button input
u32 kDown = hidKeysDown();  // just pressed this frame
u32 kHeld = hidKeysHeld();  // held this frame
// KEY_A, KEY_B, KEY_L, KEY_R, KEY_START, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT

// citro2d drawing
C2D_DrawRectSolid(x, y, z_depth, w, h, color);  // filled rectangle
C2D_DrawText(&text_obj, flags, x, y, z, scalex, scaley);
C2D_TextBufClear(textBuf);
C2D_TextParse(&text_obj, textBuf, "string");

// Colors (RGBA8 format for C2D)
u32 C2D_Color32(r, g, b, a);  // 0-255 each

// 3DS screen dimensions
// Top: 400 wide × 240 tall (GFX_TOP)
// Bottom: 320 wide × 240 tall (GFX_BOTTOM)

// IP address
#include <unistd.h>
u32 ip = (u32)gethostid();
// bytes: ip&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF

// File I/O (standard C FILE* works fine on 3DS via devkitARM)
// SD card root: "sdmc:/"
// 3DS system save: uses FSUSER_* API (not used in this project)

// Threads
#include <3ds/thread.h>
Thread t = threadCreate(my_func, arg, stackSize, priority, processor, detach);
// priority: 49 is normal. Lower number = higher priority.
// processor: -1 = default, 1 = second core (New 3DS only)
threadJoin(t, U64_MAX);
threadFree(t);

// Mutexes / LightLock (for GPS fix protection)
LightLock lock;
LightLock_Init(&lock);
LightLock_Lock(&lock);
LightLock_Unlock(&lock);
```

---

## Architecture Decisions Log

| Decision | Reason |
|----------|--------|
| libcurl for HTTPS tiles | httpc only supports TLS 1.0, rejected by modern tile servers |
| mbedTLS for GPS server | Only working TLS library for 3DS; OpenSSL PRNG fails |
| Self-contained QR encoder | No QR lib in devkitPro portlibs; custom ~600 line encoder avoids any dep |
| stb_image for JPEG satellite decode | Simple, header-only, works on 3DS; decode runs on worker thread to avoid main-thread freeze |
| libpng for PNG street tiles | Faster for typical low-complexity map graphics; `decode_png` produces Morton ABGR in one pass |
| strstr/sscanf JSON parsing | No JSON library needed for the few fixed-format responses we parse |
| socInit owned by network.c | First module to init wins; GPS server skips gracefully if already done |
| Log to sdmc file | Console output not visible on 3DS; SD log is the only practical debug channel |
| Embedded cert in header | No filesystem read needed at startup; cert bytes baked in at compile time |
| sslcInit in main.c at startup | mbedTLS entropy backend requires this; must come before any mbedTLS use |

---

## AI Coding Guidelines For This Project

1. **Never add OpenSSL**. It doesn't work on 3DS.
2. **Never call socInit() twice** without handling the duplicate-init case.
3. **Always use `log_write()` for any new operations** that could fail on-device — the log file is the only debug output.
4. **Check all 3DS `Result` codes** — use `R_FAILED(result)` macro.
5. **Memory is tight on O3DS** (~64MB total, shared by OS + app). Don't allocate large buffers on the stack. Use `malloc`/`free`.
6. **The Makefile auto-compiles everything in `source/`**. To add a new file, just create `source/myfile.c` and add a header to `include/`. No Makefile edits needed.
7. **citro2d drawing must happen inside a frame** (`C3D_FrameBegin` / `C3D_FrameEnd`). Never draw outside the frame.
8. **GPS server runs on a background thread**. Any shared state between the GPS thread and main thread must be protected with `LightLock`.
9. **QR code display replaces the top screen map** — it's not an overlay. The condition is: `g_screen_mode == APP_SCREEN_SETTINGS && g_settings_tab == SETTINGS_TAB_GPS && g_qr_valid`.
10. **The bottom screen UI always renders** — only the top screen changes between map and QR.

