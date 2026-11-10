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
5. On the bottom touchscreen, drag the map and tap to place a guess. Press A
   or tap GUESS to submit. The result screen shows the actual location,
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
- **A:** submit when a pin is placed, or advance from a result screen.
- **B / Start:** pause; B from pause returns to the StreetGuess menu. B again
  from menu exits to the unchanged normal Maps view.

The current game renderer uses the shared Google preview and progressive
higher-quality imagery routines. No Google image cache or persistent panorama
index is created.

## Candidate sampling caveat

This is **not yet the proposed OpenStreetMap road-centerline data pack**.
The self-contained sampler generates a continuous range of coordinates near
many worldwide population centers and in broad geographic regions. It does
not select from a small, fixed panorama list. Google validates coverage at
runtime, with a bounded 24-candidate attempt count and a 35-km recent-round
proximity filter. It cannot currently guarantee worldwide road accuracy,
balanced coverage, acceptable loading time, or near-zero repetition across
multiple launches. The proposed large road-geometry pack remains a separate
feasibility and packaging task.

United States and Europe modes currently use coarse geographic bounding
boxes; they do not enforce precise political boundaries.

## Host tests

From the repository root:

```sh
sh tests/run-guess-all-host.sh
git diff --check
```

The new suites cover deterministic state, five rounds, scoring and Haversine
geography, antimeridian wrapping, 48,000 generated candidates, timer and pause
behavior, simulated Google-only round success/denial/no-coverage, rule
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
