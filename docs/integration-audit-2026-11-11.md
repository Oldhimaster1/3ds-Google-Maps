# 3DS Google Maps — feature integration audit (2026-11-11)

**Scope:** source tree at merge commit `bce76c1531c604eab90a469ae7d136e5452f5112`. The branch merges `earth3d-combined-continuation` (`d29a2d48`) and `earth3d-streetview-ux-upgrade` (`a6cfc529`) without altering either parent branch.

**Evidence grading:** CONNECTED = actual controls/runtime use the feature's implementation; PARTIAL = source exists but does not deliver all requested behavior; UNVERIFIED = depends on real services, 3DS build, emulator or hardware testing; EXCLUDED = intentionally deferred. CONNECTED does not mean physically verified.

## Feature trace

| Request | Evidence / integration | Verdict |
| --- | --- | --- |
| Normal interactive maps, zoom, caching, offline tilepacks/regions | `main.c`, `map_tiles.c`, `tilepack.c`, `region.c`, `network.c` | CONNECTED; regression unverified |
| Live visible-first tiles / GPS-follow prioritization | `map_tiles.c` viewport queue / two workers, `tile_priority`; `main.c` GPS camera updates | CONNECTED; throughput unverified |
| GPS QR + browser location bridge | `main.c` QR display, `qrcode.c`, `gps_server.c` HTTPS/ack, `gps.c` | CONNECTED; scan/phone hardware unverified |
| GPS speed and course | `gps_server.c` parses speed/heading, `gps_motion.c`, `gps.c`, `main.c` HUD | CONNECTED |
| Search improvements | `main.c` nearby/wider results, search cache, `search_input.c`, `geo_formats.c` | CONNECTED |
| Multiple coordinate formats | `geo_formats.c` decimal, DMS, cardinal hemisphere, full 10-digit Plus Codes; short codes excluded | CONNECTED within stated limits |
| Multi-source live Street View | `panorama_live.h` provider enum, `pano_runtime.c`, `street_sources.c` | CONNECTED but external-provider dependent |
| Source selection | Auto, Panoramax, Mapillary, KartaView, experimental Google, experimental Bing | CONNECTED; Streetlevel and Apple Look Around absent |
| Panorama navigation (L/R and touch links) | `main.c`, `pano_runtime_step_at`, `pano_runtime_follow`, `street_nav.c` | CONNECTED; real provider navigation unverified |
| Failed navigation preserves visible photo / recent cache | `pano_runtime_poll`, `street_recent.c`, `explore_load_street_pixels` | CONNECTED |
| Gyro stabilization/recalibration | `main.c`, `pano_motion.c`, `pano_gyro.c`, `guess_ui.c` | CONNECTED; sensor orientation/drift unverified on physical devices |
| Normal Street View D-pad zoom | `main.c` adjusts FoV ~28–85 degrees | CONNECTED; **no zoom-triggered zoom-2+ imagery** |
| Progressive Google preview→sharp panorama | `pano_runtime.c` zoom-0 preview, two zoom-1 tiles, `explore.c` 1024x512 texture | CONNECTED, subject to unofficial endpoint availability |
| View-dependent ultra-high-resolution panoramas | `pano_detail.c` selects zoom-2 to zoom-4 visible tiles but never called by the image download/render pipeline | **NOT INTEGRATED** |
| Panorama quality setting | `earth_options.c`, `main.c` menu | **PARTIAL: value stored, not used by loader** |
| Street View fly-in/timing | `main.c`, `street_fly_logic.c` and loading overlap | CONNECTED; actual transition quality unverified |
| Real terrain outside Grand Canyon | `terrain_runtime.c` on-demand S3 Terrarium, `terrain_math.c`, `explore_apply_terrain` | CONNECTED; live service/hardware unverified |
| Exact selected-point 3D focus/city zoom | `terrain_select_zoom` up to z15, focus offsets in `explore.c`, parent-tile fallback | CONNECTED in this merged branch |
| Terrain detail and stereoscopic scene | `explore.c` 65x65 geometry and eye-dependent stereo projection | CONNECTED; one tile at a time, height shading rather than satellite texture |
| Actual OSM 3D buildings | `building_runtime.c` Overpass query, `building_mesh.c` extrusion, `explore_apply_buildings` stereo draw | CONNECTED; opt-in OFF by default, max 48 buildings, simple ways only |
| Terrain/building geographic alignment | `terrain_runtime_source_zoom` on parent fallback, `focus_x/z` applied to ground and building meshes | CONNECTED in this merged branch |
| Stereoscopic building toggle | `main.c`, `earth_options.c` | CONNECTED; must enable and receive OSM results |
| Detailed interactive globe | Existing Blue Marble globe, zoom 0 auto-enter; `globe_detail.c` calculates prospective patches | **PARTIAL: progressive live high-detail globe not connected** |
| Globe detail menu | `main.c` and `earth_options.c` | **PARTIAL: saved but not applied to renderer** |
| Advanced settings | Input speed, deadzone, invert gyro, exaggerated terrain, buildings toggle; persistent file | CONNECTED for listed controls; no arbitrary rebindable keymap |
| Text size, weight and high contrast | `ui_accessibility.c`, `main.c`, `guess_ui.c` | CONNECTED; legibility requires screenshots |
| Address panel and search result readability | Existing layout and multiline changes in `main.c` | CONNECTED; layout unverified |
| StreetGuess separate Google-only game | `guess_ui.c`, `pano_runtime_start_game_google`, `guess_core.c` | CONNECTED; experimental Google access may block play |
| StreetGuess modes/timers/score/replay | `guess_core.c`, `guess_replay.c`, `guess_ui.c`, roadpack optional | CONNECTED; no guarantee a random coordinate has imagery |
| StreetGuess Balanced/Urban/Rural/Broad setting | `guess_ui_enter` reads `EARTH_OPTIONS_FILE` into `ui.game.sampler`; `guess_core.c` changes sampling | **CONNECTED** |
| Live route progress and remaining distance | `route_follow.c`, `main.c`, GPS fix sequence | CONNECTED |
| ETA and off-route rerouting | Proportional duration estimate, 3 distinct off-route fixes and 30-second cooldown | CONNECTED; not traffic-aware; end-to-end unverified |
| Turn-by-turn route instructions | Explicitly excluded by user | EXCLUDED |

## Integration change in this branch

The source now has *both* GPS route-follow improvements and the later terrain/building fixes. The combined branch previously lacked 16 commits from the UX branch addressing exact terrain centering, city-scale zoom, fallback elevation, building geographic alignment, and building worker test coverage. This merge imports those files and makes the three necessary `main.c` call changes without replacing the routing code.

## Hard blockers before calling the full upgrade finished

1. Implement actual **zoom-2+ view-dependent panorama retrieval, memory budget/cancellation, tile-to-view mapping and GPU blending/overlay**, not just the tile selection helper. Connect panorama-quality setting. Verify provider terms and access.
2. Implement **globe detail** patches from `globe_detail.c` into the actual globe renderer, or label menu as unavailable.
3. Decide whether **rebindable controls** are required beyond speed/deadzone/inversion.
4. Run all host suites, devkitARM/3ds-dev build and package both 3DSX/CIA. Current GitHub CI fails with zero reported job steps; that does not diagnose compile errors.
5. Test live terrain, OSM buildings, 3D slider, gyro, StreetGuess, GPS QR and provider navigation on Old 3DS and New 3DS/XL, check memory and frame pacing.
6. Create verified binary artifacts and hashes only after complete build; do not reuse stale `preview-builds/` binaries.

**Do not promote this experimental source merge as a finished release or create an FBI install QR from it.**
