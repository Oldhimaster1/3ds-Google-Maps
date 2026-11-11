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
