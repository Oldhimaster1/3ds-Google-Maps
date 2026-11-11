# RC3 verification — November 11, 2026

RC3 source: `9e09e27a12d88dee8de5aca5983f5d4a245b5999`.
Package commit: `9cc4858b5fca0cb8d3806ead7ed0b960c7f144fb`.

## What changed

Country Streak saves personal bests immediately, including a run ended before a
miss. Its result screen shows countries in a row rather than a five-round score.
The regression plays 12 correct answers over two batch boundaries, misses,
restarts, reloads records, and loads the older record format.

The challenge collection contains 32 Panoramax locations in 21 countries. It is
still Europe-heavy. Mystery Expeditions choose five landmark stops; daily and
friend games share versioned locations, while local pass and play gives everyone
the same rounds. Missing images offer retry without consuming a round.

## Evidence

| Check | Result |
| --- | --- |
| Full host gate | [Recovered full gate](host-tests.txt): every suite passed, including UI motion and Country Streak. |
| Clean build | [Recovered clean build](build.txt): all application sources compiled and CIA/3DSX packaged. Existing assembler EOF and GNU-stack warnings remain. |
| Source/package match | Local built 3DSX equals the uploaded RC3 3DSX; SOURCE_COMMIT records the exact source revision. |
| Public downloads | Both commit-pinned raw downloads fetched without credentials and matched SHA256SUMS after recovery. |
| RC2 preservation | No RC2 paths changed between the previous source and RC3 package commit. |
| QR | Generated for the immutable RC3 CIA URL; independently decoded before commit. |
| Azahar | RC3 launches with llvmpipe/OpenGL. Cached satellite imagery renders; globe rendering recovered. Online requests fail hostname lookup in this environment. |
| Hardware | Original/New 3DS, FBI install, gyro, stereo, phone pairing and performance remain unverified. |

Checksums:

```text
38fd7f0b8d42f337e2be6a4b1ac4c9b45563f4c13e62005c8cbc5efdb76bbff8  3ds_google_maps.cia
8e64eeb6980bb86f80e24ae37f8a0378ffc17d070ec63f1209b9710ba3de9a72  3ds_google_maps.3dsx
```

No stable release, stable tag, main merge or announcement is authorized.

The focused UI/game regression was rerun after recovery and passed with
`ASAN_OPTIONS=detect_leaks=0` because this container cannot run LeakSanitizer
under ptrace. AddressSanitizer and UBSan remained enabled. See
[country-streak-recheck.txt](country-streak-recheck.txt).

## Feature scope and known limits

These details accompany the preview download. They describe implemented scope,
not proof that each feature has been tested on hardware.

| Experience | Included behavior | Important limit |
| --- | --- | --- |
| Maps and offline regions | Pan, search, favorites, named packs, pause/resume and integrity checks | Download only from sources permitting offline use. |
| Street View | Multi-provider explorer, gyro controls, navigation and fly-in | Physical gyro/navigation unverified; Google/Bing remain experimental. |
| Panorama detail | View-selected Google zoom 2–4 curved detail overlay | **One central tile**, with low-resolution fallback; provider authorization unresolved. |
| Globe | Rotatable Blue Marble globe with on-demand NASA GIBS detail and optional stereo | Historical composite, **not real-time satellite photography**; one detail patch. |
| Terrain and buildings | Selected-location Terrarium elevation; optional real OSM building footprints | Single terrain tile; simple buildings only; no fabricated fallback structures. |
| Classic StreetGuess | World / US / Europe; Moving / No Move / NMPZ; timers and records | Google-only random mode depends on experimental access and coverage. |
| Daily and friend challenges | Exact versioned locations, five rounds, three difficulties, UTC records/history | **32 locations in 21 countries**, still Europe-heavy. Correct console UTC clock required. |
| Pass and play | Two to four players, identical rounds, handoff screens and final scores | Local trust-based play; no accounts or online leaderboard. |
| Discovery modes | Surprise Me, five-stop Mystery Expeditions and Country Streak with saved personal bests | Curated locations, not an unlimited worldwide library. Streaks continue beyond five answers. |
| Custom controls | Four profiles, eleven digital buttons, conflict-free swaps and saved settings | START is fixed. StreetGuess captions follow your selected bindings; some older screens retain default captions. |
| Routing | Route overlay, remaining distance, estimated ETA and off-route reroute logic | No verified full turn-by-turn guidance. |


## Controls, network and provider checks

Settings → More → page 3 contains four remap profiles, action selection, physical
binding and restore-defaults. Assigning an occupied button swaps its old action.
Touchscreen and Circle Pad access remain available. Text size, weight, contrast,
stick sensitivity and gyro options are retained. Some old captions still show
logical/default buttons; see the verification report before relying on remapping.

## Phone GPS

Use the same trusted local Wi-Fi network. Start the GPS server and scan its
**private session QR** with the phone. The browser must accept the local
self-signed certificate and allow location access; some phones may block this
flow. This candidate generates a fresh certificate and secret each session,
requires authenticated submissions, and removes exact positions from phone status
text and request URLs from network logs. Physical pairing remains unverified.

Keep the browser in the foreground. A locked screen may pause updates. Rescan after
restarting the server or reloading a tab that has lost its pairing token. Do not
include the pairing QR, private coordinates or addresses in shared screenshots.
[Security and provider review](../rc1/provider-review.md)


For troubleshooting, see the [usage guide](../../usage.md#troubleshoot).
For incomplete integration checks, see the [source audit](../../integration-audit-2026-11-11.md)
and [provider review](../rc1/provider-review.md).
