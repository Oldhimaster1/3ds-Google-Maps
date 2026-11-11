# Usage and build reference

[Back to the project README](../README.md)

Open **R → Explore** for the world globe, selected-location 3D terrain,
nearby buildings, Street View Explorer and StreetGuess. See [Explore controls and data credits](explore.md).

## Map and settings

The top screen displays the map. The touch panel opens **Search**, **Favorites**, **Recents**, **Route**, **Identify**, **GPS**, **Track**, and **Menu**. Zoom out with **Y** from Z1 to Z0; another deliberate **Y** press at Z0 opens the 3D globe at the same coordinates. Zoom into the globe with **X** until the normal map returns. Use the D-Pad or Circle Pad to pan, **X/Y** to change zoom, and **Start** to exit. You can drag in the upper area of the touch panel to pan. The app saves the view, layer, night mode, GPS mode, and related settings when it exits normally.

In **Menu → Explore**, drag the **Fly-in** slider to set the Street View entry animation from Off to 1600 ms. Imagery searches begin as the transition starts; no unrelated offline demo image appears. In **Menu → View**, choose **Street** or **Sat** and adjust night mode or the debug overlay. Press **Select** in that tab for per-tile SD, network, decode, and GPU upload timings. In **Menu → Data**, manage the optional LAN proxy, browsing cache, and offline packs. Clearing the browsing cache preserves saved packs.

When using the default search service, choose **A** to prefer places near the map center or **X** to search everywhere.

## Tile sources

Live browsing and the named offline download source are configured separately. The default Street layer uses OpenStreetMap’s standard raster tile service. Its [tile usage policy](https://operations.osmfoundation.org/policies/tiles/) prohibits offline bulk downloads. The default [Esri World Imagery service](https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer?f=pjson) reports export disabled. Use a source that permits offline downloads.

Offline packs use USGS The National Map by default. **Street** uses USGS Topo and **Sat** uses USGS Imagery Only. Start with a small area in the contiguous United States. Outside supported coverage, use **Menu → Data → Offline regions → L** to enter a provider that permits offline downloads in your area. **L** shows the active endpoint before you edit it. A saved layer URL takes precedence, followed by the enabled LAN proxy, then the built-in preset.

Each URL template must include `{z}`, `{x}`, and `{y}` in the order the service expects. Use 256 × 256 PNG or JPEG tiles. An extensionless base URL works for services that accept `/z/x/y`. The region must be resumed with the same endpoint. Named packs support up to 4,096 tiles each and up to 12 installed packs, in addition to the two legacy packs.

### Manage saved regions

| In Offline regions | Action |
| --- | --- |
| **Y** | Plan a new pack |
| **L** | View or change the download source for the current layer |
| **R** | Toggle coverage. Green means an exact tile is saved at the current zoom. Amber means no pack tile exists at that zoom. |
| **A** | Preview a completed pack or resume a paused one |
| **Select** | Check the selected completed pack’s tile images |
| **X**, then **X** | Remove the selected pack |
| **B** | Back, or pause when downloading |

In **New offline pack**, Left/Right changes the minimum zoom and Up/Down changes the maximum. **X** switches between the visible area and a corridor if a route is active. Download narrow ranges first, especially at high zoom. Live search and calculating new routes require internet access. The route corridor contains image tiles, so reopening the pack does not restore the route line.

The downloader uses two persistent connections and writes completed tiles into the partial pack one at a time. The transfer screen reports tiles per second, payload MB downloaded in the current session, estimated time remaining, and current free SD-card space. Temporary failures are retried with exponential backoff; HTTP 429 and 503 responses honor `Retry-After`. Pausing remains responsive during a provider cooldown, and resuming skips tiles already saved.

## Phone GPS

The 3DS has no built-in GPS. In server mode, the app runs a local HTTPS page and uses your phone browser’s [Geolocation API](https://developer.mozilla.org/en-US/docs/Web/API/Geolocation_API). Both devices need a local network path to each other. Guest networks with client isolation can block access even if both devices show Wi-Fi connectivity.

1. On the console, open **Menu → GPS** and tap **Server**.
2. Scan the local pairing QR with your phone. On a network you trust, check that the page address matches the local address shown on your console. The browser may ask about the self-signed certificate. If it refuses the page or location permission, use a compatible setup or NMEA client mode.
3. Allow the page to access location. Leave it in the foreground. Return to the map and press **A** to follow the current fix. **B** on the map connects GPS in the selected mode.

Azahar was tested with a simulated HTTPS fix. Browser and certificate behavior on a physical phone can vary, and no iPhone or Android browser is claimed verified for this candidate. In NMEA client mode, `sdmc:/3ds_google_maps/gps.cfg` can contain one line with the phone server’s IPv4 address and TCP port separated by a space. Fixes expire after ten seconds without an update.

## Troubleshoot

- **Offline map is blank or blurred when you zoom:** select the completed pack, press **A** to preview, and return to a zoom level in the pack. A coarser cached tile may show temporarily as a zoom preview. Turn on coverage to see where exact tiles are available.
- **Transfer stops:** check Wi-Fi, free SD space, the tile source and its expected zoom levels. Press **A** on the paused pack to continue with the same endpoint. Press **Select** on a completed pack to check image readability.
- **Search or route fails:** confirm internet access and retry. These requests use external services. Press **B** to cancel a pending request.
- **Phone does not connect:** make sure the local page is reachable and location is allowed. Wait for a fresh fix, then enable follow mode. Do not share the pairing QR or the local address.
- **Need help:** report your device model, app version or commit, steps, and a sanitized log if available. Logs can contain request URLs and locations.

## Build from source

Install [devkitPro and the Nintendo 3DS toolchain](https://devkitpro.org/wiki/Getting_Started). In the devkitPro shell:

```sh
dkp-pacman -S 3ds-dev 3ds-curl 3ds-mbedtls 3ds-libpng 3ds-jansson
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
make clean
make
./tests/run-host.sh
```

On Windows, use the corresponding MSYS2 path to the devkitPro installation, such as `/c/devkitPro`. The build produces `3ds_google_maps.3dsx` and a development ELF. The host tests cover region planning, antimeridian wrapping, and interrupted writes.

### Build the CIA

Put [bannertool 1.2.3](https://github.com/carstene1ns/3ds-bannertool/releases/tag/1.2.3) and [makerom 0.18.4](https://github.com/3DSGuy/Project_CTR/releases/tag/makerom-v0.18.4) on your path, then run `make cia` with the same devkitPro environment. Use `BANNERTOOL=/path/to/bannertool` and `MAKEROM=/path/to/makerom` to override the tool paths. Makerom 0.18.4 writes the intended minor version into the title metadata.

The CIA uses the project's `icon.png` and `banner.png`, and `banner.wav` supplies the HOME Menu chime. Its title ID is `000400000FD34B00`. Keep this ID for future CIA upgrades and increase the title version in the Makefile. Makerom generates a fresh ticket on each run, so compare the executable and embedded assets when checking reproducibility. Compare the entire CIA against the release checksum when verifying a download.

### SD layout

```text
sdmc:/3ds/3ds_google_maps/3ds_google_maps.3dsx
sdmc:/3ds_google_maps_debug.log
sdmc:/3ds_google_maps/
  regions/              named .tilepack files and .part downloads
  tiles/                browsing cache and legacy sat.tilepack/street.tilepack
  offline-sat.cfg       optional imagery download URL
  offline-street.cfg    optional street download URL
  session.cfg           saved map view and settings
  favorites.txt         saved places
  gps.cfg               optional NMEA client address and port
  proxy.cfg             optional compatible LAN proxy
```

### Tilepack and rendering details

The file starts with `3DTP`, version, tile count, and data offset in a 16-byte little-endian header. Each sorted 20-byte entry records zoom, coordinates, offset, and byte count. The payload contains PNG or JPEG bytes. The app uses binary search on the index and serializes reads from a shared pack file. A partial download stores the plan and completed payloads so interrupted transfers skip tiles already on disk.

An optional [desktop tilepack tool](../tools/download_region.py) creates or merges legacy packs and deduplicates identical tile images with SHA-256. Run it with `--help` for the options. It writes to `./3ds_google_maps/tiles/` by default. Use only providers and endpoints that permit your intended offline use. The existing script targets Esri browsing services by default, so check service terms before using it.

Tile decoding and download work happen off the main render thread. The GPU receives tiled textures, and cached parent or child tiles can cover a zoom transition while exact tiles load. The [source](../source) contains the network, offline storage, UI, GPS, and rendering code.
