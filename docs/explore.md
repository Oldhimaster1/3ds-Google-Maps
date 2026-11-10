# Explore on Nintendo 3DS

Open **R → Explore** and tap a scene. All three scenes work with Wi-Fi off. **B** returns to the previous map. **A** opens the map at the scene's location. The regular map, route, GPS and offline-pack controls are unchanged.

| Scene | Controls and coverage |
| --- | --- |
| World globe | Circle Pad or D-Pad rotates across all longitudes and up to 89° latitude. **X / Y** zoom. Press **X** at the closest zoom or **A** for an animated dive into the existing map at the selected coordinates. The 2D map clamps to the Web Mercator latitude limit and loads details from its normal sources. |
| Terrain in 3D | Orbit and zoom a roughly 32 × 29 km Grand Canyon relief model. Raise the 3D slider for distinct left and right views on a stereoscopic console. With the slider down or on 2DS, the same relief is visible in mono. Elevation is exaggerated 2.2× for readability. |
| Street View Explorer | First searches the public Panoramax catalog for real 360° photos near your **current map center**. **L / R** or Left / Right moves through linked photos in the sequence (or nearby candidates). Circle Pad looks around; **X** enables gyro, **Select** reverses gyro direction and **Y** retries the search. If offline, unavailable, or no panoramas exist nearby, the two included Bethesda photos remain available as a fallback. |


## Live panoramas — current limitations

Live Street View uses the open Panoramax federated catalog at
[api.panoramax.xyz](https://api.panoramax.xyz/api). It queries a small bounding
box around the map center, expands the search only if necessary, accepts only
images whose STAC \`pers:interior_orientation.field_of_view\` is at least 340
degrees, and downloads the selected panorama via the provider's \`sd.jpg\`
endpoint. The image is decoded on a background CPU thread and rescaled to a
1024 × 512 RGB565 GPU texture; camera drawing stays on the render thread.

Coverage depends on contributors and is **not** comparable with Google's
commercial Street View coverage. No API key is required. Individual picture
licenses and contributors vary; the bottom screen attempts to display their
metadata. The provider catalogs are publicly reusable subject to their
specific licenses. The 3DS must be online for uncached live images. The app
does not persist newly downloaded live panoramas.

For low memory usage, images above 2048 × 1024 or 5 MiB compressed are
rejected rather than risking an Old 3DS crash. Unsupported images are skipped
in favor of nearby alternatives when possible. The live provider may be
unreachable on networks with unsupported TLS or DNS paths. HTTP requests
currently inherit the existing app's libcurl certificate-checking configuration;
**certificate verification needs a separate security review before production
use**. This branch should be treated as a hardware-test preview, not a verified
retail-ready release.

### Test on original Nintendo 3DS

1. Select a map coordinate with Panoramax 360° photo coverage; enter Street
   View, verify the label changes to **Panoramax live 360 image**, and check
   correct imagery and coordinates when returning with **A**.
2. Try **L** and **R** to follow the sequence and **Y** to refresh.
3. Enable **X** gyro and physically turn left/right and tilt up/down; press
   **Select** if the hardware's apparent gyro directions need reversing.
4. With Wi-Fi disconnected, confirm both bundled demonstration panoramas still
   work. Test **B**, re-entry, repeated scene changes, and memory stability.
5. Run with **View → Debug overlay** enabled, record FPS and time needed to
   find/download a live image; verify the lower-screen labels don't overlap.

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
