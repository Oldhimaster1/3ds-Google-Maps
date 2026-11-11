# Progressive globe, view-dependent panorama detail, and control remapping

**Branch:** `feature-live-detail-and-remap-20261111`  
**Parent:** `earth3d-consolidated-audit-20261111`  
**Scope:** source only; do not publish or install the existing `preview-builds/` binaries as builds of this branch.

## Features implemented

### 1. View-dependent Google panorama detail

- The original live Street View pipeline remains intact: zoom-0 preview followed by the existing 1024×512 image (including the Google zoom-1 two-tile upgrade).
- `detail_plan.c` uses `pano_detail_visible()` and camera yaw, pitch and field of view to select the **closest visible 512×512 imagery tile** at Google zoom 2, 3 or 4.
- The Panorama Quality setting now actively controls tile detail: **Fast** = no extra tile, **Standard** = zoom 2, **Detail** = zoom 3 (zoom 4 at narrow FOV), **Auto** = zoom 2 or 3 based on the field of view.
- `detail_runtime.c` runs **one asynchronous worker** only after the current panorama preview/upgrade worker finishes, debounces camera motion (220 ms), validates tile dimensions, and retains the base image after any failed request.
- `explore.c` uploads a single 512×512 RGB565 tile (~512 KiB GPU memory) and maps it onto a correctly positioned curved section of the existing inside-facing sphere. This is *real local source-image detail*, not an upscaled base panorama.
- A cancelled request never overwrites the current panorama. A deliberately cancelled request does **not** trigger the 30-second provider error cooldown. Source data is never written to an SD image cache.
- Both ordinary Street View and Google-powered StreetGuess call this path. For other providers the existing 1024×512 fallback remains. **The patch is limited to the central visible tile**, not every tile of the user's entire screen, to protect original-3DS memory and bandwidth.
- The Google Street View tile endpoint is an **experimental, unofficial** provider integration. Tile availability, terms, rate limits and permissions must be verified before public production release.

### 2. Progressive online globe imagery

- The globe retains the bundled NASA Blue Marble texture as its always-available offline fallback.
- `globe_detail.c` now participates in a real streaming path: current camera longitude/latitude/zoom, globe detail setting, and NASA GIBS WMTS XYZ URL.
- The background worker fetches one 256×256 NASA Blue Marble Next Generation tile at a time, validates image dimensions, converts to RGB565 and overlays it on the outside of the globe with **Mercator-correct geographic alignment** (`detail_mesh.c`).
- Globe Detail **Low / Auto / High** now influences the requested geographic zoom. No extra tiles are prefetched.
- The 3D slider can optionally enable stereo globe rendering; the same detail patch is rendered in both eye passes.
- **Live means online on-demand tile loading; it does not mean real-time satellite imagery.** NASA Blue Marble is a static historical composite. Visible attribution: NASA GIBS / Blue Marble.
- If there is no Wi-Fi or the NASA server refuses a request, the existing globe remains usable.

### 3. Digital control remapping

- Four separately saved mapping profiles: **Map & menus, Street View, Globe & terrain, and StreetGuess**.
- In **Settings → More → page 3/3**, tap Control Profile to choose a mode; Remap Logical Action selects an action; Bind Physical Button waits for a physical keypress; Reset This Profile restores just that mode.
- Supports every in-app digital A/B/X/Y/L/R/D-pad/Select button through a **collision-free bijective swap**. No duplicated or missing physical bindings.
- Physical **START stays fixed** as the emergency exit and cancels input capture. Touch and analog Circle Pad are not remapped; the existing speed/deadzone settings still control analog behavior. System software keyboard, Home button, volume, and 3D slider are outside the app's remapping scope.
- Mappings persist at `sdmc:/3ds_google_maps/control_map.cfg`, loaded with strict validation; corrupt files fall back to defaults.
- Both the game and ordinary map/modal UI use the same shared key map. Labels referring to A/B/etc describe *logical actions* and may differ from physical keys after remapping.

## Source paths

`source/detail_plan.c`, `source/detail_mesh.c`, `source/detail_runtime.c`, `source/input_remap.c` and matching headers; existing integration in `source/explore.c`, `source/main.c`, and `source/guess_ui.c`.

The Makefile discovers `source/*.c` automatically, so no manual `CFILES` list changes are required.

## Tests added

Run from the repo root on a compatible GCC/Clang host:

```sh
sh tests/run-input-remap-host.sh
sh tests/run-detail-plan-host.sh
sh tests/run-detail-mesh-host.sh
sh tests/run-detail-runtime-host.sh
sh tests/run-feature-integration-host.sh
```

- New pure-C planning, geometry and key mapping test cases include deterministic 100,000-iteration randomized regressions; ASAN/UBSAN host builds were exercised during development against corresponding local source snapshots.
- Mocked detail-worker tests exercise one-at-a-time scheduling, completed uploads, provider prioritization, stale-request cancellation, rapid direction reversal, failed-network fallback, no repeated download for a loaded tile, and cleanup.
- `tests/run-feature-integration-host.sh` also runs existing Earth, terrain/building, route-follow and StreetGuess host regression suites.
- Source wiring checks ensure the key modules are actually called and that renderer upload/draw methods are present.

**Validation limitations:** No complete repository checkout was available to the build environment; a full set of host scripts has **not** been executed from a fresh repository checkout. No devkitARM/3ds-dev compile, CIA/3DSX packaging, Azahar launch, or 3DS hardware runtime verification was performed. GitHub Actions jobs may not start due to account billing restrictions. A passing isolated C host test cannot establish GPU correctness or real image-provider access.

## Instructions for the next builder AI

1. Check out *this exact branch at the current head*, not `main`, `earth3d-combined-continuation`, or old preview binaries.
2. Run `sh tests/run-feature-integration-host.sh` with ASAN/UBSAN-capable host toolchain. Fix any real failures without silently excluding tests.
3. Perform a complete devkitARM/3ds-dev clean build. Verify **both** CIA and 3DSX are produced from the same commit; inspect link-time errors, shader data, textures and compile warnings. Do not claim tested until actual build succeeds.
4. Validate heap/linear VRAM usage and the GPU's two-eye detail patch, especially during repeated Street View navigation and rapid switching to/from globe, terrain, and StreetGuess.
5. Verify real NASA/GIBS requests and any Google provider terms/authorization and rate limits. Do not assume a successful mock request proves endpoint availability.
6. On a separate preview branch or release asset, publish binaries with full SHA-256 checksums. Do not overwrite the user's known-good CIA.
7. Perform physical Old 3DS and New 3DS/XL UI, stereo/slider, memory, cache, cancel, remapping/persistence, loss-of-Wi-Fi, and StreetGuess regression tests before calling this production ready.

## Physical-device acceptance checklist

- [ ] Open world globe with and without Wi-Fi; rotate/zoom while observing NASA detail status; confirm fallback remains visible
- [ ] Raise/lower 3D slider on globe and terrain; inspect stereo alignment and crashes
- [ ] Open 3D terrain in mountains and in cities; verify centered geographic relief and building meshes where OSM data exists
- [ ] Enter experimental Google panorama where coverage exists, rotate with D-pad/gyro, zoom in; compare Panorama Quality Fast versus Detail
- [ ] Navigate across panorama IDs while a detail tile is downloading; verify old detail never appears on the next image
- [ ] Repeatedly rotate across tile boundaries, reverse direction, and switch back to Maps; check responsiveness and memory
- [ ] Test all four remap profiles: swap B with another button, confirm behavior, restart app and confirm persistence, then reset
- [ ] Confirm START still exits/cancels binding and touch access to Settings still works
- [ ] Play StreetGuess and verify controls, gyro, records, game loading and new detail path
- [ ] Verify phone GPS QR, search, route progress and reroute are unaffected
