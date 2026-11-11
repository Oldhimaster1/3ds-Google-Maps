> Historical handoff. For current RC1 build/test results and unresolved blockers, see [RC1 acceptance](release/rc1/acceptance.md).

# 3D Earth + Street View integrated development status

This is a **source-only experimental development branch**, starting from
`gps-search-live-tiles-fix`. It is **not a hardware-verified release**.

## Implemented in the branch

- Expanded Earth/Street settings: stick speed/dead zone, gyro inversion, image
  quality selection, elevation exaggeration, building toggle and globe detail
  selection (some quality modes remain presentation/configuration only).
- Coordinate parsing for decimal, degree/minute/second and full Plus Codes.
- GPS speed/heading data helpers, world-heading transfer between panoramas,
  fly-in / panorama transition refinements, and persistent StreetGuess
  geographic replay avoidance and selection modes.
- New **on-demand, live elevation**: the current map coordinate starts an
  asynchronous 256x256 Terrarium tile request using the published Mapzen/AWS
  elevation-tiles-prod S3 dataset. Its 33x33 elevation samples update the
  existing 65x65 stereo terrain mesh, with shaded height color. The exact
  selected map position is an offset within the tile, applied consistently
  to terrain and buildings. Returning to the map preserves the entry point.
  Source: `terrain_runtime.c`, `terrain_math.c`, `explore.c`.
- Optional nearby **OSM 3D building geometry**: when Buildings is enabled,
  a bounded on-demand Overpass request looks for a few nearby building ways.
  Valid simple footprints are extruded using heights or floor estimates,
  aligned to elevations sampled from the selected Terrarium tile, and drawn
  by the existing per-eye Citro3D stereo projection. No placeholder buildings
  are generated when the provider is unavailable.
- Clear status text for live elevation / optional nearby buildings, and
  attribution to Mapzen and OpenStreetMap contributors.

## Deliberate limitations

- Only **one elevation tile** is loaded at a time. The result can display
  meaningful local 3D relief, but there is not yet a multi-tile streaming
  quadtree, terrain edge stitching, adjacent-tile paging, or geographic
  colored satellite texture. Elevation z15 requests may not be present in
  all regions. No offline DEM pack is required.
- Live building data is **opt-in**, default OFF. Public Overpass capacity is
  shared; this interactive request is capped (radius 220m, up to 60 returned
  ways, 512 KiB HTTP response, max 48 generated buildings) and should not
  be called repeatedly for a moving 3D camera. The small parser currently
  supports simple building ways only; holes, multipolygons, roof shapes,
  buildings crossing the tile, and complex outlines require future work.
- The `pano_detail.c` module selects candidate high-resolution visible
  panorama tiles, **but actual zoom-2+ image download and GPU overlay are
  not yet connected**. The current renderer still uses a 1024x512 panorama.
- The `globe_detail.c` module chooses progressive Web Mercator patches,
  **but live patch downloads and higher-resolution globe rendering are not
  yet connected**. The existing Blue Marble globe is still displayed.
- The settings have input sensitivity and dead-zone options; they are
  **not yet a complete user-rebindable control map**. The gyro still needs
  hardware calibration.
- No turn-by-turn routing implementation is included in this branch, per
  the user's explicit request.
- Geospatial data/imagery providers have separate licenses and acceptable
  usage limits. Google Street View imagery access through the project's
  experimental direct endpoint is **not equivalent to an authorized official
  Google Maps Platform API session**.

## Regression tests

```sh
sh tests/run-earth3d-host.sh
```

This runs the new pure math, orientation, sampling and settings suites, plus
the isolated simulated **terrain network worker** and **building geometry
worker** tests with AddressSanitizer/UndefinedBehaviorSanitizer, a mocked
3DS thread scheduler, deterministic HTTP/image responses and real C building
geometry.

The following were actually exercised in the model's working environment:
- Exact committed terrain runtime module plus mathematical Terrarium decoder,
  under GCC + ASAN/UBSAN, including successful 256px decoding, cancellation /
  failed requests, invalid coordinates, antimeridian tile URL and cleanup.
  The city-zoom/focus variant was compiled from a byte-identical copy of the
  committed runtime module and passed on the same host.
- Committed building runtime and building mesh modules against deterministic
  Jansson/HTTP mocks; checks include valid building geometry, terrain-aligned
  top/bottom vertices, URL building, failed requests, and cleanup.
- Lexical structure checks on touched C and header modules.

**Still required before claiming a working CIA:**
1. Full devkitPro/3ds-dev compilation, CIA and 3DSX packaging and host tests
   from the complete repository checkout.
2. Azahar launch and UI/graphics screenshots, including left/right frame
   comparisons and the effect of the hardware stereo setting.
3. Real physical Old 3DS and New 3DS tests for live S3 and Overpass
   availability, display correctness, VRAM/RAM usage, 3D slider disparity,
   frame rate, cancellation, error behavior, and GPS/Map/StreetGuess regressions.
4. Final high-res panorama and globe renderer integration if those advanced
   features are part of the intended release.
5. Provider attribution and permitted-use verification.

Do not replace the user's working installed CIA or produce an FBI QR for this
branch until a complete binary build and download checksum verification passes.
