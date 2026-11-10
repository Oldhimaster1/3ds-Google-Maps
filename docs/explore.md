# Explore on Nintendo 3DS

Open **R → Explore** and tap a scene. Globe and Grand Canyon terrain work offline; live Street View requires a supported imagery source and Wi-Fi. **B** returns to the current scene's map position. The regular map, route, GPS and offline-pack controls are unchanged.

| Scene | Controls and coverage |
| --- | --- |
| World globe | Circle Pad or D-Pad rotates across all longitudes and up to 89° latitude. **X / Y** zoom. Press **X** at the closest zoom or **A** for an animated dive into the existing map at the selected coordinates. The 2D map clamps to the Web Mercator latitude limit and loads details from its normal sources. |
| Terrain in 3D | Orbit and zoom a roughly 32 × 29 km Grand Canyon relief model. Raise the 3D slider for distinct left and right views on a stereoscopic console. With the slider down or on 2DS, the same relief is visible in mono. Elevation is exaggerated 2.2× for readability. |
| Street View Explorer | Searches imagery near the **current map center**. Tap the source row to choose **Automatic, Panoramax, Mapillary, KartaView, Google (experimental), or Bing (experimental)**; **Y** remains a cycling shortcut. **R** follows a verified forward link, **L** backtracks when history exists, and touchscreen arrows choose intersection branches. **Down** retries; **X** toggles gyro, **Up** recalibrates and **Select** reverses gyro motion. The bottom minimap tracks live panoramas; available higher-quality Google tiles replace the initial preview when valid. Ordinary street photos remain flat. The bundled demonstration panoramas no longer display or ship in the app. Entry starts a real provider search immediately; the overhead map flies down while imagery downloads. |


## StreetGuess development game

**Explore → Play StreetGuess** opens a completely separate five-round
Google Street View geography game with its own world/US/Europe random
location sampling, Moving/No Move/NMPZ rules, independent touchscreen
guessing map, scores and local records. B from the game menu returns to
ordinary Maps at the unchanged location and settings.

This is a **development feature**, not a released or hardware-verified
game. [Read StreetGuess controls, implementation details, test coverage
and remaining verification gates](streetguess.md).

## Multi-provider Street View

The app runs native lightweight HTTPS/JSON client code on the original
Nintendo 3DS, with a single download worker. The provider preference is
saved to `sdmc:/3ds_google_maps/street_provider.cfg` and the imagery-dot
toggle to `street_coverage.cfg`. The fly-in duration (Off to 1600 ms, default
700 ms) is adjustable with the Explore-settings touchscreen slider and saved
to `street_fly.cfg`. These files do not contain credentials.

| Provider | Setup | Notes |
| --- | --- | --- |
| Automatic | None | Tries Mapillary first **if a token is configured**, then Panoramax and finally KartaView. Google and Bing remain experimental explicit selections. |
| Panoramax | No key | Open federated STAC search; genuine 360° images, progressive low-res thumbnail while the SD image loads in the background. Individual licenses and attribution apply. |
| Mapillary | Own free API token | Request nearby photos from the Graph API, preferably true spherical panoramas. Regular photos display as flat images. |
| KartaView | No key | Experimental: small-radius public search, mainly **flat** street photos. Public API may time out or require authentication. Do not misrepresent these as 360° panoramas. |

### Mapillary setup (optional)

1. Obtain a personal Mapillary API access token through the official developer
   portal. Do **not** commit your token to GitHub or distribute it in a CIA.
2. Create `sdmc:/3ds_google_maps/mapillary.token` containing your token
   on a single line (such as `MLY|...`), without surrounding quotes.
3. Select **Mapillary** or **Automatic** using **Y** on the Street Explorer
   bottom screen. The app only sends the token to Mapillary's Graph API
   and enables TLS certificate verification for that request. Depending on
   your libcurl / mbedTLS configuration, you may need an updated trusted CA
   bundle for this to work on an original 3DS.

### Speed and reliability

Searches request at most 24 Panoramax / Mapillary results initially and try
small geographical areas before widening them. **KartaView is searched only
when selected manually**, with a strict request timeout and a 140 m maximum
radius. JPEG decoding and CPU Morton conversion take place on a worker;
texture uploads and geometry updates stay on the render thread.

Panoramax thumbnail previews are displayed quickly when available, and the
full SD image replaces them in the background. Failed upgrades preserve the
usable preview. When moving to another panorama, known metadata is reused
instead of making a redundant metadata request. Two previously visited Panoramax
frames can remain in a bounded in-session RAM history; it is cleared on exit.
Google imagery is not cached or prefetched, in light of provider restrictions.
While a linked image downloads the current view remains visible; the new view
is briefly faded in when ready, and the viewing angle stays unchanged. Image downloads are limited
to 4 MiB compressed and 2048×1024 decoded pixels to bound memory use.

The **Street imagery map markers** toggle on the Explore settings tab displays
photo points from the most recent successful nearby search (cyan Mapillary,
green Panoramax, amber KartaView). These are **local photo points, not
continuous Google-style road coverage lines or worldwide coverage tiles**.
The markers draw without extra network requests while browsing the map.

Gyroscope readings are divided by the hardware raw-counts-per-degree-per-second
coefficient, not multiplied. Initial still-device calibration, low-pass
velocity smoothing, a motion dead zone and spike rejection reduce the
previously excessive sensitivity and jitter. No gyroscope-only solution can
guarantee absolute compass heading without drift; **Up** recalibrates the
zero bias and **Select** reverses both look directions. Physical gyro
orientation needs hardware confirmation.

### Verification still required

Build and test on an **original Nintendo 3DS**, not just New 3DS or Azahar:
provider switch, live Panoramax photos, Mapillary with a user token, flat
KartaView image, offline fallback, quick repeated L/R navigation, 30-minute
stability, Wi-Fi loss and reconnect, low SD space, gyro in four directions,
memory use and FPS. Real provider availability, rate limits and licensing
remain external dependencies. The inherited network backend disables
certificate verification for some noncredentialed requests; this must be
hardened before a production release. Mapillary credentialed requests
explicitly enable certificate validation.

## New York imagery and input troubleshooting

The Street View API selector is **not** Google's proprietary Street View.
In Automatic mode, Mapillary is used **only if its own client token file**
(`sdmc:/3ds_google_maps/mapillary.token`) exists. Panoramax is otherwise
searched, followed by KartaView. Crowdsourced availability is uneven even
in cities with comprehensive Google Street View coverage.

The older build often incorrectly reported **no imagery** because it
examined only 24 Panoramax records in a narrow area and looked for the wrong
STAC `pers:projection` field location. The newer search processes up to 80
records, expands across three ranges (~300 m / 1.5 km / 5 km), recognizes
equirectangular projection, accepts ordinary street photographs in the flat
viewer, and checks KartaView as the last automatic fallback. An empty
provider response never proves that the city lacks imagery.

When using the Circle Pad, a dominant-axis lock removes small vertical
analog cross-talk while turning horizontally. This is only applied to
Street View; the globe and terrain keep their existing controls. The
gyroscope now requires 45 stationary samples before movement is accepted,
rejects noisy cross-axis readings, and stops when rotation settles.
**Up** recalibrates; hold the console still and watch for the calibration
message to disappear.

Individual photographs may have an inherently tilted horizon due to how
they were captured. The viewer currently does not use full sensor-fusion
roll correction or per-image leveling metadata, so these cases require
additional original-3DS validation and a separate renderer correction.
No actual New York coverage or on-device gyro stability has been verified
for this branch; host tests don't establish live provider compatibility.

## Old 3DS design

The release CIA uses the 64 MiB application memory profile. The renderer draws one indexed mesh on the PICA200 with an RGB565 texture, using the existing Citro2D UI on the other screen. Sphere scenes draw 4,096 triangles and use a 1 MiB texture plus about 66 KiB of geometry. Terrain draws 8,192 triangles per eye and uses 128 KiB of texture plus 131 KiB of geometry. The extra stereo render target uses approximately 563 KiB for color and depth before alignment. The second eye is submitted only with a raised slider. Mesh generation and decompression happen on entry, and GPU resources are synchronized before deletion. Street entry uses a tiny blank GPU texture rather than a bundled panorama. The fly-in reuses cached map tiles, without downloading additional imagery. Image replacement preserves the old GPU texture until the new allocation and upload succeed. The existing 2D map cache remains available.

| Approach | Decision |
| --- | --- |
| CPU reprojection vs. GPU perspective | A perspective sphere handles the photo seam and poles and avoids per-pixel ARM work. A fixed camera and bounded geometry keep render costs predictable. |
| Global tiled globe vs. a world texture | One NASA mosaic provides immediate offline coverage. It hands off to Web Mercator at regional scale. A continuous LOD surface would need its own tile hierarchy, texture budgets, edge stitching and verified providers. The current handoff is an animated zoom and fade. |
| Regional mesh vs. live elevation and buildings | One verified USGS DEM enables real stereoscopic relief without live TIFF decoding, tile coordination or height guesses. Worldwide terrain would need a separate service and LOD system. Building extrusion would need licensed footprints, height data and a geometry pipeline. This build has one region and zero buildings. |
| Live imagery without fictional fallbacks | Only real results from the user-selected source display in Street View. No unrelated demo scene appears when no coverage exists. Google remains experimental and subject to imagery access, attribution and caching restrictions. Nonpanoramic photos are drawn flat rather than projected onto a sphere. |

Old 3DS frame rate, GPU timing, cold entry latency, physical slider behavior and gyro calibration require device measurements. The existing **View → Debug overlay** enables the FPS readout in Explore. Target 30 FPS or better with stereo enabled, then profile frame time and linear heap on the console before enabling wider data coverage.

## Data credits and regeneration

- **Globe:** [NASA/GSFC Scientific Visualization Studio, Blue Marble](https://svs.gsfc.nasa.gov/2915/). Blue Marble data courtesy of Reto Stöckli and NASA's Earth Observatory. Use the 1024 × 512 PNG as `earth.png`.
- **Terrain:** Map services and data available from U.S. Geological Survey, National Geospatial Program. [3DEP elevation export](https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer/exportImage). Request `bbox=-112.31,35.96,-111.95,36.22&bboxSR=4326&imageSR=4326&size=128,128&adjustAspectRatio=false&format=tiff&pixelType=F32&interpolation=RSP_BilinearInterpolation&f=image` as `canyon.tif`.

With NumPy and Pillow installed, run `python3 tools/prepare_explore_assets.py <input-directory> data`. The output contains little-endian, Morton-tiled RGB565 textures compressed with zlib, plus an unsigned 16-bit metre grid. The normal build uses the checked-in files and needs no asset downloads.

### Technical references

- [Original 3DS hardware](https://3dbrew.org/wiki/Hardware) and [application memory layout](https://www.3dbrew.org/wiki/Memory_map)
- [devkitPro's Citro3D stereo rendering example](https://github.com/devkitPro/3ds-examples/blob/master/graphics/gpu/lenny/source/main.c)
- [NOAA ETOPO global relief](https://www.ncei.noaa.gov/products/etopo-global-relief-model) and [USGS 3DEP](https://www.usgs.gov/faqs/what-types-elevation-datasets-are-available-what-formats-do-they-come-and-where-can-i-download)
- [MapillaryJS](https://github.com/mapillary/mapillary-js), [KartaView API](https://kartaview.org/doc/), [Google Street View tiles](https://developers.google.com/maps/documentation/tile/streetview) and [Map Tiles API policies](https://developers.google.com/maps/documentation/tile/policies)

### Experimental direct providers

In Street View Explorer, press **Y** to select **Google (experimental)** or
**Bing (experimental)**. Both request imagery directly over the console's Wi-Fi.
Automatic mode uses the existing Panoramax, Mapillary, and KartaView sources.
Use **L/R** to follow available links and **Down** to search again. Google
currently searches the coverage tile containing the map center, so its links
are limited to that tile. Bing loads six low-resolution JPEG faces.

The experimental endpoints may change or return HTTP 403 or 429. During
development, Times Square metadata worked for both providers and Bing returned
all six JPEG faces. Google's imagery endpoint returned HTTP 403. Console
performance and cube face alignment still need to be checked on an Old 3DS.
