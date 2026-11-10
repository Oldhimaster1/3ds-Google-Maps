# Explore on Nintendo 3DS

Open **R → Explore** and tap a scene. All three scenes work with Wi-Fi off. **B** returns to the previous map. **A** opens the map at the scene's location. The regular map, route, GPS and offline-pack controls are unchanged.

| Scene | Controls and coverage |
| --- | --- |
| World globe | Circle Pad or D-Pad rotates across all longitudes and up to 89° latitude. **X / Y** zoom. Press **X** at the closest zoom or **A** for an animated dive into the existing map at the selected coordinates. The 2D map clamps to the Web Mercator latitude limit and loads details from its normal sources. |
| Terrain in 3D | Orbit and zoom a roughly 32 × 29 km Grand Canyon relief model. Raise the 3D slider for distinct left and right views on a stereoscopic console. With the slider down or on 2DS, the same relief is visible in mono. Elevation is exaggerated 2.2× for readability. |
| Street View Explorer | Searches nearby **360° photographs** using the provider chosen in **R → Explore**: Auto, Panoramax, Mapillary or KartaView 360. **L / R** navigates linked/nearby photos; Circle Pad looks around; **X** enables gyro; hold still for calibration, **D-Up** recalibrates, **Select** reverses motion and **Y** retries. Bundled Bethesda photos remain available when offline or unsupported. |


## Imagery providers and controls

Tap the **provider row** at the top of **R → Explore** to cycle:

| Option | Coverage | Setup | What renders |
| --- | --- | --- | --- |
| **Automatic** (default) | Tries Mapillary when configured, then Panoramax, then KartaView | None for Panoramax/KartaView | **360° panoramas only**; chooses the first nearby provider with usable imagery |
| **Panoramax** | Uneven, strong in Europe | No API key | True 360° equirectangular imagery |
| **Mapillary** | Global crowdsourced data, including more U.S. sequences | User-supplied read-only **client access token** | Only spherical 360° images, not ordinary forward-looking photographs |
| **KartaView 360** | Global crowdsourced sequences | No API key for public search | Only photos explicitly marked with 360° FOV and compatible spherical projection |

**Mapillary token:** register your own application at the official Mapillary developer site and put the **client** access token (not an app secret) on one line in:

`sdmc:/3ds_google_maps/mapillary-token.txt`

The token isn't committed to GitHub or included in packaged CIA/3DSX builds. The app enforces HTTPS certificate verification when requesting Mapillary imagery; this can fail if the console has no usable trust store. You can supply a current trusted CA bundle at `sdmc:/3ds_google_maps/cacert.pem` (obtain it from an authoritative CA-bundle publisher, for example [curl.se CA Extract](https://curl.se/docs/caextract.html)). Do not disable TLS verification to work around token errors. **Automatic** mode skips Mapillary when the token isn't configured. No Google API keys or billing are required for the default setup.

**Speed strategy:** search a tight nearby area first, fetch at most 24 Panoramax results, and use short bounded requests. Panoramax first displays a quick thumbnail on the existing 3D sphere and upgrades it with the standard-definition photo asynchronously, without blocking Circle Pad input. Mapillary requests a 1024-pixel thumbnail directly. Broken or oversized images are skipped when alternatives are available. All photos use the shared 1024×512 RGB565 GPU texture.

**Gyro controls:** press **X** to enable, then hold the 3DS still for about 0.3–0.6 seconds. The filtered gyro uses the console's yaw and pitch channels, rejects spikes, limits camera speed and adapts its stationary bias. Press **D-Up** to recalibrate while still, **Select** to reverse both axes. Gyro input is optional; Circle Pad always works.

**Nearby 360 photo markers:** in the Explore settings menu, tap the final bar to enable or disable colored dots on the regular map. Green = Mapillary, blue = Panoramax, yellow = KartaView. **This shows only the nearby results from the most recent successful Street View search**, not comprehensive citywide coverage lines; it does not automatically crawl roads. Turning off markers also avoids map overlay work.

**Known limitations:** coverage depends on contributors. Mapillary/KartaView sequence navigation currently uses the nearest results returned by the location search; Panoramax additionally supports its linked previous/next STAC images. KartaView often has flat photos, which are intentionally excluded from this 360° renderer. An unsuccessful search does not mean no photographs exist on the street. Native Old 3DS, TLS, rate-limit, and long-session testing are still required before calling this production-ready.

## Old 3DS design

The release CIA uses the 64 MiB application memory profile. The renderer draws one indexed mesh on the PICA200 with an RGB565 texture, using the existing Citro2D UI on the other screen. Sphere scenes draw 4,096 triangles and use a 1 MiB texture plus about 66 KiB of geometry. Terrain draws 8,192 triangles per eye and uses 128 KiB of texture plus 131 KiB of geometry. The extra stereo render target uses approximately 563 KiB for color and depth before alignment. The second eye is submitted only with a raised slider. Mesh generation and decompression happen on entry, and GPU resources are synchronized before deletion. Switching street stops briefly uses two 1 MiB textures to preserve the current view if the new allocation fails. The existing 2D map cache remains available.

| Approach | Decision |
| --- | --- |
| CPU reprojection vs. GPU perspective | A perspective sphere handles the photo seam and poles and avoids per-pixel ARM work. A fixed camera and bounded geometry keep render costs predictable. |
| Global tiled globe vs. a world texture | One NASA mosaic provides immediate offline coverage. It hands off to Web Mercator at regional scale. A continuous LOD surface would need its own tile hierarchy, texture budgets, edge stitching and verified providers. The current handoff is an animated zoom and fade. |
| Regional mesh vs. live elevation and buildings | One verified USGS DEM enables real stereoscopic relief without live TIFF decoding, tile coordination or height guesses. Worldwide terrain would need a separate service and LOD system. Building extrusion would need licensed footprints, height data and a geometry pipeline. This build has one region and zero buildings. |
| Licensed captures vs. live panorama discovery | The two CC BY captures are usable without credentials or server availability. Mapillary and KartaView are candidates for wider coverage after checking API requirements and coverage. Google's official Street View Tile API requires API/session setup and separate compliance for attribution and content storage. A flat crop of a panoramic image was rejected because it distorts viewing near the poles and cannot represent perspective correctly. |

Old 3DS frame rate, GPU timing, cold entry latency, physical slider behavior and gyro calibration require device measurements. The existing **View → Debug overlay** enables the FPS readout in Explore. Target 30 FPS or better with stereo enabled, then profile frame time and linear heap on the console before enabling wider data coverage.

## Data credits and regeneration

- **Globe:** [NASA/GSFC Scientific Visualization Studio, Blue Marble](https://svs.gsfc.nasa.gov/2915/). Blue Marble data courtesy of Reto Stöckli and NASA's Earth Observatory. Use the 1024 × 512 PNG as `earth.png`.
- **Terrain:** Map services and data available from U.S. Geological Survey, National Geospatial Program. [3DEP elevation export](https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer/exportImage). Request `bbox=-112.31,35.96,-111.95,36.22&bboxSR=4326&imageSR=4326&size=128,128&adjustAspectRatio=false&format=tiff&pixelType=F32&interpolation=RSP_BilinearInterpolation&f=image` as `canyon.tif`.
- **Street photographs:** Copyright 2025 G. Edward Johnson. [Norfolk Avenue](https://commons.wikimedia.org/wiki/File:Norfolk_Ave_and_Woodmont_Ave_360_panorama_Bethesda_MD_2025-07-27_09-46-42_1.jpg) and [Old Georgetown Road](https://commons.wikimedia.org/wiki/File:Old_Georgetown_Rd_and_Woodmont_Ave_360_panorama_Bethesda_MD_2025-07-27_09-43-32_1.jpg), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Use the 1280-pixel previews as `norfolk.jpg` and `georgetown.jpg`. The images were resized and color-quantized for the console.

With NumPy and Pillow installed, run `python3 tools/prepare_explore_assets.py <input-directory> data`. The output contains little-endian, Morton-tiled RGB565 textures compressed with zlib, plus an unsigned 16-bit metre grid. The normal build uses the checked-in files and needs no asset downloads.

### Technical references

- [Original 3DS hardware](https://3dbrew.org/wiki/Hardware) and [application memory layout](https://www.3dbrew.org/wiki/Memory_map)
- [devkitPro's Citro3D stereo rendering example](https://github.com/devkitPro/3ds-examples/blob/master/graphics/gpu/lenny/source/main.c)
- [NOAA ETOPO global relief](https://www.ncei.noaa.gov/products/etopo-global-relief-model) and [USGS 3DEP](https://www.usgs.gov/faqs/what-types-elevation-datasets-are-available-what-formats-do-they-come-and-where-can-i-download)
- [MapillaryJS](https://github.com/mapillary/mapillary-js), [KartaView API](https://kartaview.org/doc/), [Google Street View tiles](https://developers.google.com/maps/documentation/tile/streetview) and [Map Tiles API policies](https://developers.google.com/maps/documentation/tile/policies)
