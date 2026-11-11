# StreetGuess development mode

StreetGuess is a separate Google-only location guessing game inside **Explore → Play StreetGuess**.
It is isolated from the ordinary maps, GPS, globe, terrain and Street View UI. The
normal map camera is not used as the guessing map camera and the saved Street View
provider is not changed to Google just to play a game.

**Development status: source implementation candidate.** The game core and a
simulated 3DS interface have passing host tests. A complete devkitPro build,
Azahar startup, real Google 360 imagery and original Nintendo 3DS hardware
tests have **not** passed or been performed yet. Do not publish this as a
verified game release until those are complete.

## Game loop

1. Open Explore's **Play StreetGuess** card.
2. On the game menu, choose World / United States / Europe, Moving / No Move /
   NMPZ, and a round timer. **A** starts five rounds.
3. The app generates a continuously randomized geographic coordinate from
   global region and population-area priors, checks Google Street View live,
   and accepts only a usable Google 360 panorama reasonably near that point.
   A rejected request is retried with a bounded attempt count.
4. When the first usable panorama loads, the timer begins. The secret start
   coordinate is not shared with the guessing map. There is no fly-down
   showing the hidden location.
5. Press A to focus the bottom guessing map, then drag and tap to place a
   guess. Press A again or tap GUESS to submit. The result screen shows the actual location,
   distance and points. Press A to advance.
6. After five rounds see the result summary. Local best scores are saved to
   `sdmc:/3ds_google_maps/streetguess_records.txt`.

## Game controls

- **Circle Pad / D-Pad Left and Right:** look around (except NMPZ).
- **Select:** gyro toggle (disabled in NMPZ).
- **L / R:** move along linked Google panoramas in Moving mode only.
- **D-Pad Down:** panorama zoom out outside NMPZ.
- **Bottom-left RETURN TO START button:** go back to the starting Google panorama
  in Moving mode. It is separate from zoom controls.
- **D-Pad Up:** panorama zoom in outside NMPZ.
- **X / Y:** zoom the bottom guess map.
- **Touchscreen:** drag to pan the guessing map, tap to place a pin.
- **A:** focus the guessing map. When a pin is selected, A submits the guess;
  on a result screen A advances. The top-screen dialog explains these steps.
- **B:** closes the focused guess map; otherwise pauses. From pause, B
  returns to the StreetGuess menu. From menu, B exits to normal Maps.
- **Start:** pauses the game regardless of map focus.
- **Y on game menu:** toggles the short first-time tutorial.

The current game renderer uses the shared Google preview and progressive
higher-quality imagery routines. No Google image cache or persistent panorama
index is created.

## Random round generation and optional road packs

The default self-contained sampler generates a continuous range of coordinates
near worldwide population centers and in broad geographic regions. It is **not**
a small list of predetermined Google panoramas. Live Google Street View
validation selects actual playable 360-degree imagery, while a rolling
35-km recent-round filter rejects nearby repeated starts. The default
sampler does not know which geographic points actually contain roads, so
its live success rate and first-image latency still need on-device testing.

**New:** an optional, binary **road-geometry pack reader** is implemented in
`source/guess_roads.c`. When the SD card contains
`sdmc:/3ds_google_maps/streetguess_roads.bin`, StreetGuess samples random
points on independently sourced road segments and then validates them using
Google's live Street View request path. No Google images, IDs, or coverage
indexes are stored in this pack. If the pack is unavailable, invalid, or has
no suitable road for the chosen region, the default sampler still works.

To generate a pack from your own properly licensed road GeoJSON or
line-delimited GeoJSON/NDJSON sources on a desktop:

```sh
python3 tools/build_guess_roads.py \
  --input global_roads.ndjson world \
  --input us_roads.ndjson usa \
  --input eu_roads.ndjson europe \
  --output streetguess_roads.bin \
  --max-records 150000 --segment-km 0.7 --seed 1
```

Copy the resulting file to `/3ds_google_maps/streetguess_roads.bin` on your
3DS SD card. You do not need to operate a server or provide an API token for
this independent geometry pack. The builder filters for normal drivable
public roads, subdivides long segments into compact records and bounds the
pack to 1,000,000 records (20 MB of geometry plus a 16-byte header). The
3DS seeks to individual records without loading the whole file into RAM.
World and region-specific input groups must be tagged with the appropriate
`--input <file> <region>` argument; they do not automatically infer accurate
national borders. Follow the OpenStreetMap ODbL attribution and database
requirements for any OSM-derived geometry you ship.

**A complete worldwide road pack is not bundled or validated yet.** This
branch ships the generator and optional reader, not a downloaded global
road dataset. Any real pack needs data provenance, geographic QA and
on-device performance/coverage benchmarks. The source falls back
automatically until such a pack exists.

The numeric sampling stress test covers 48,000 candidate coordinates across
5,935 distinct one-degree geographic cells with independently mixed test
seeds. **This is not a count of confirmed Google Street View panoramas.**
United States and Europe fallback modes still use coarse bounding boxes,
not exact political boundaries.

## Host tests

From the repository root:

```sh
sh tests/run-guess-all-host.sh
git diff --check
```

The new suites cover deterministic state, five rounds, scoring and Haversine
geography, antimeridian wrapping, 48,000 generated candidates, timer and pause
behavior, simulated Google-only round success/denial/no-coverage, generated road-pack\nroundtrip and integrated UI selection, rule
restrictions, independent map coordinates and returning to Maps.

Run the rest of the repository's host suites as well.

## Required before a public CIA

- Cross-compile with devkitPro; test both CIA and 3DSX. The source integration
  has not been cross-compiled in this environment.
- Launch in Azahar and test all menus, five rounds, all movement rules,
  touchscreen map, pause/quit, timer expiry, results, and record persistence.
- Test on an **original** 3DS with live Google Street View. Measure location
  discovery time, coverage success rates, imagery load, memory high-water
  mark and worst-frame latency.
- Confirm no accidental real-coordinate leaks before submitting the guess,
  including debug and provider status views, and verify attribution remains.
- Check the normal maps, routing, GPS, offline packs, street view, fly-in,
  minimap, gyro, globe and terrain after returning from a game.
- Investigate Google's published API and permitted-use requirements before
  publicly distributing a game based on its imagery. The existing
  experimental direct endpoint working does not establish authorized reuse.
- Keep binaries on a separate preview path, verify direct downloads against
  local SHA-256, then generate and decode-verify any FBI installation QR code.

This file records what is actually implemented and distinguishes simulated
tests from real network/hardware evidence.
