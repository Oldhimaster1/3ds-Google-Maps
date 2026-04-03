# 3DS Google Maps

A fully-featured interactive maps application for Nintendo 3DS homebrew. Uses OpenStreetMap tiles downloaded directly over HTTPS (no proxy needed), dual-screen map display, GPS tracking via a phone browser, touch/D-pad navigation, satellite imagery, route planning, place search, favorites, and more.

> **Status**: Working and actively developed. HTTPS tile downloads work natively on-device using libcurl + mbedTLS.

---

## Features

### Map
- **Dual screen display** — top screen shows the map, bottom screen shows the UI panel
- **Touch panning** — drag on the bottom screen to pan the map
- **D-pad navigation** — pan in any direction
- **Circle pad** — smooth continuous pan
- **L/R buttons** — zoom in/out
- **Crosshair** — red crosshair always marks the center of the map
- **Night mode** — dark color overlay for night use
- **Debug overlay** — shows zoom level, tile stats, FPS

### Tiles
- **OpenStreetMap street tiles** — downloaded directly over HTTPS (`tile.openstreetmap.org`)
- **Satellite imagery** — via Esri World Imagery (switchable in settings)
- **SD card tile cache** — tiles saved to `sdmc:/3ds_google_maps/tiles/` so they reload instantly without WiFi
- **RAM tile cache** — up to 64 tiles cached in memory
- **Prefetch ring** — configurable ring of tiles prefetched around the current view
- **Optional proxy** — can route tile requests through a local Python proxy for debugging

### GPS
- **HTTPS server mode** — 3DS runs an HTTPS server on port 8443; open the URL on your phone in a browser and it streams GPS coordinates back to the 3DS using the browser's Geolocation API
- **NMEA TCP client mode** — connect to a GPS2IP or similar NMEA forwarder app on your phone
- **GPS tracking** — map auto-centers on your current GPS position
- **QR code** — when the GPS server starts, a scannable QR code appears on the top screen encoding the server URL so you can open it on your phone without typing

### Navigation
- **Place search** — search for addresses and places (via Nominatim/OpenStreetMap geocoding)
- **Route planning** — enter start and destination, get a route drawn on the map
- **Reverse geocode** — tap "Identify" to find out what's at the center of the map
- **Favorites** — save and recall locations, stored to `sdmc:/3ds_google_maps/favorites.txt`
- **Recents** — last 10 viewed locations remembered in-session

### UI
- **Settings panel** — full settings menu on the bottom screen with tabs: View, Tiles, Data, GPS
- **Status bar** — bottom of screen shows current action/status
- **Street view panoramas** — basic panorama viewer (requires proxy)

---

## Controls

| Input | Action |
|-------|--------|
| D-Pad | Pan map |
| Circle Pad | Smooth pan |
| L Button | Zoom in |
| R Button | Zoom out |
| Touch screen drag | Pan map |
| Touch bottom panel buttons | Search / Favorites / Recents / Route / GPS / Track / Menu |
| A | Toggle GPS tracking |
| B | Close settings menu |
| START | Exit |

---

## GPS Phone Bridge — How It Works

The 3DS runs a TLS 1.2 HTTPS server (mbedTLS) on port 8443. When you tap **Server** in the GPS settings:

1. The 3DS starts listening on `https://<3DS-IP>:8443`
2. A QR code appears on the top screen encoding that URL
3. Scan it with your phone — the browser opens and asks for location permission
4. The phone's browser sends GPS coordinates to the 3DS every second via `POST /location` with JSON `{"lat": ..., "lon": ...}`
5. The map follows your real position

The cert is self-signed (embedded in `include/gps_server_cert.h`) so browsers will show a security warning — tap "Advanced → Proceed" to continue.

---

## Building

### Prerequisites

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS packages, then install the required portlibs:

```bash
dkp-pacman -S 3ds-dev
dkp-pacman -S 3ds-curl 3ds-mbedtls 3ds-libpng
```

### Build

Open the devkitPro MSYS2 shell and run:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH

cd /path/to/3ds_google_maps_src
make
```

Output: `3ds_google_maps.3dsx`

On Windows with devkitPro installed to `C:\devkitPro`:

```bash
export DEVKITPRO=/c/devkitPro
export DEVKITARM=/c/devkitPro/devkitARM
export PATH=/c/devkitPro/devkitARM/bin:/c/devkitPro/tools/bin:$PATH
cd /c/path/to/3ds_google_maps_src
make
```

### Linked libraries

```
-lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lpng -lz -lcitro2d -lcitro3d -lctru -lm
```

---

## Installation

1. Copy `3ds_google_maps.3dsx` to `sdmc:/3ds/3ds_google_maps/` on your 3DS SD card
2. Launch via the Homebrew Launcher
3. Make sure WiFi is connected before starting

App data is saved to `sdmc:/3ds_google_maps/`:
- `tiles/` — cached map tiles
- `favorites.txt` — saved favorite locations
- `log.txt` — debug log (overwritten each launch)

## Releases

This repository includes a GitHub Actions workflow at `.github/workflows/release.yml` that builds `3ds_google_maps.3dsx` and attaches it to a GitHub release whenever you push a version tag that starts with `v`.

To publish a release:

```bash
git tag v1.0.0
git push origin v1.0.0
```

If you just want to confirm the project still builds in GitHub Actions, run the workflow manually from the **Actions** tab. Manual runs upload the `.3dsx` as a workflow artifact without creating a GitHub release.

---

## File Structure

```
source/
  main.c          — Main loop, input, UI rendering, settings panel, GPS QR display
  network.c       — Tile downloading (libcurl for HTTPS, httpc for HTTP), disk cache, proxy
  map_tiles.c     — Tile coordinate math, RAM cache, prefetch, citro2d rendering
  gps.c           — GPS dispatch layer (client/server mode switching)
  gps_server.c    — HTTPS GPS server (mbedTLS TLS 1.2, serves HTML page, receives POST /location)
  logging.c       — File logger (writes to sdmc:/3ds_google_maps/log.txt)
  qrcode.c        — Self-contained QR code encoder (byte mode, EC level L, versions 1-4)
  simple_png.c    — Minimal PNG loader wrapper
  stb_image.c     — stb_image implementation file

include/
  network.h       — Tile download API, proxy/cache settings, TileSource enum
  map_tiles.h     — Tile render/cache API
  gps.h           — GPS source mode API (client vs server, init/cleanup, fix access)
  gps_server.h    — GPS HTTPS server public API
  gps_server_cert.h — Embedded DER self-signed cert + RSA-2048 PKCS#1 key (valid 2026-2036)
  qrcode.h        — qr_encode() API
  logging.h       — log_write() API
  stb_image.h     — stb_image header
  simple_png.h    — simple_png header

tools/
  tile_proxy.py   — Local HTTP proxy that re-serves OSM/Esri tiles (for debugging, not needed)
  generate_cert.py — Generated the embedded TLS cert/key in gps_server_cert.h
  oracle_proxy.py — Old proxy script (obsolete, tiles now fetched directly via HTTPS)

Makefile          — devkitARM build config
```

---

## Architecture Notes for AI Assistants

### TLS / Networking
- **Tile downloads**: libcurl 8.4.0 with mbedTLS backend. `download_url_curl()` in `network.c` handles HTTPS. The old `httpc` (3DS system HTTP) is still used for plain HTTP fallback. SSL verification is disabled (`CURLOPT_SSL_VERIFYPEER = 0`) because the 3DS cert store is empty.
- **GPS server**: mbedTLS 2.28.8 TLS server. Uses `MBEDTLS_ENTROPY_HARDWARE_ALT` via `sslcGenerateRandomData()` for PRNG — requires `sslcInit(0)` to be called at app start (done in `main.c`).
- **SOC service**: `socInit()` is called once by `network.c` during `net_curl_init()`. The GPS server detects if it's already initialized and skips it gracefully.
- **Why not OpenSSL**: OpenSSL 3.x DRBG/PRNG system cannot initialize on 3DS. mbedTLS is the correct solution — it's in the official devkitPro package repo.

### Screen Layout
- Top screen: 400×240, shows map tiles + overlays (route, GPS marker, night tint, or QR code)
- Bottom screen: 320×240, shows UI panel (either `render_bottom_panel()` or `render_settings_panel()`)
- `g_screen_mode` controls which panel is shown (`APP_SCREEN_MAIN` or `APP_SCREEN_SETTINGS`)

### Settings System
- `g_settings_tab` — which tab is active (`SETTINGS_TAB_VIEW/TILES/DATA/GPS`)
- `get_settings_hit()` — hit-tests touch position against all interactive areas, returns `SettingsHitTarget` enum value
- `apply_settings_hit()` — dispatches the action for whichever thing was tapped
- `is_recent_settings_hit()` — returns true if a given button was tapped within 180ms (used for visual press feedback)

### QR Code System
- `qrcode.c` is a fully self-contained QR encoder — no external libraries
- Supports byte mode, EC level L, versions 1-4 (up to ~80 bytes — enough for `https://xxx.xxx.xxx.xxx:8443`)
- `g_qr_valid`, `g_qr_modules[]`, `g_qr_size` are global state in `main.c`
- QR is lazily generated in `render_settings_panel()` whenever the GPS tab is open, server is running, and `g_qr_valid == false`
- QR replaces the top screen map while Settings → GPS tab is open and server is running

### GPS State Machine
- `gps_get_source_mode()` — returns `GPS_SOURCE_NMEA_CLIENT` or `GPS_SOURCE_HTTPS_SERVER`
- `gps_is_connected()` — for server mode returns `gps_server_is_running()`, for client mode returns socket connected
- `gps_init()` / `gps_cleanup()` — start/stop whichever mode is selected
- `gps_inject_fix(lat, lon)` — called by the GPS server background thread when a phone POST arrives; protected by `LightLock`
- Tapping **Server** in settings auto-starts the server AND generates the QR immediately

### Logging
- `log_write(fmt, ...)` writes to `sdmc:/3ds_google_maps/log.txt`
- Key log prefixes: `[NET]`, `[GPS-SVR]`, `[SETTINGS]`, `[TILES]`
- Always check this file first when debugging — it's the only way to see what's failing on-device

---

## Known Issues / Things To Be Aware Of

- The GPS server's self-signed cert will show a browser security warning — this is expected, tap "Advanced → Proceed"
- On O3DS (original 3DS) the TLS handshake for the GPS server takes ~300-500ms — this is normal
- `socInit()` can only be called once per app session on 3DS — the network module owns it, the GPS server skips it if already initialized
- Tile downloads can be slow on the original 3DS (~2-4 seconds per tile at zoom 14+) — this is a hardware limitation
- The QR code only shows when Settings → GPS tab is open AND the server is running
- libcurl on 3DS does not support HTTP/2 — it uses HTTP/1.1 only
- [ ] Custom markers and points of interest
- [ ] Offline map support
- [ ] Different map styles (satellite, terrain)
- [ ] Route planning and navigation

## License

This project is developed for educational and homebrew purposes. Map data is provided by OpenStreetMap contributors under the Open Database License.

## Disclaimer

This application requires an internet connection to download map tiles. Data usage applies based on your WiFi connection. The app is intended for homebrew/development purposes only.
