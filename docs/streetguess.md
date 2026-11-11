# StreetGuess — play, explore, and challenge friends

[Back to Maps for Nintendo 3DS](../README.md)

StreetGuess is the geography game built into **Settings → Explore → Play StreetGuess**. Look around a street-level image, decide where you are, drop a pin and see how close you got. You can play alone, share the same challenge with friends or hand the console to someone nearby.

## Pick your game

| Mode | What happens |
| --- | --- |
| Classic random | Five rounds in World, US or Europe, with Moving, No Move and NMPZ rules plus timers. Searches for live experimental Google panoramas. |
| Daily Challenge | Five fixed curated panoramas for the same UTC day, dataset version and difficulty. Records are saved locally. |
| Friend Challenge | Share an SG1 code so another player can try the identical locations and difficulty settings. |
| Pass and Play | Two to four players take turns on the same console, with the same locations and a final score comparison. |
| Surprise Me | A quick game using curated places, with recent-round avoidance. |
| Mystery Expedition | Five landmark-focused stops from the curated collection. |
| Country Streak | Name the country and keep going until you miss. Your best streak is saved, even when you quit a run. |

The curated pool currently contains **32 locations in 21 countries** and is more heavily represented in Europe than other regions. Five locations are selected for each standard curated challenge. The challenge system prefers different countries within a round set when possible.

Curated games use specific Panoramax images, including creator and license information. Classic random mode relies on an experimental Google Street View integration and can fail if access or coverage is unavailable.

## Get started

1. From Maps, open **Settings → Explore → Play StreetGuess**.
2. On the game menu, press **Select** to open the challenge menu. Choose a mode with **X** or by touching its card. Use **Y** to switch difficulty.
3. Tap the player-count card for local multiplayer, or enter a friend's code through the touchscreen. You can also view your recent daily results.
4. Press **A** to start.
5. Look for clues in the panorama. For regular guessing, focus the bottom map with **A**, move or zoom it, and tap to place your pin. Press **A** or tap **GUESS** to submit.
6. Compare your guess with the real location and see your distance and score. Continue to the next round, or pass the console to the next player.

### Difficulty

- **Explorer:** Hints and no round timer.
- **Standard:** No Move and 120 seconds per round.
- **Expert:** NMPZ and 60 seconds per round.

NMPZ limits moving, panning and zooming while viewing the street. The regular guessing map remains the place to submit your answer. Country Streak uses country-name or two-letter-country-code input instead of the usual map pin.

### Controls

- **Circle Pad / D-pad:** Look around the panorama where the chosen rules allow it.
- **D-pad Up/Down:** Zoom the panorama outside NMPZ.
- **L/R:** Move along available linked viewpoints in Moving mode.
- **Touchscreen:** Pan the guessing map and place a pin; also select modes in menus.
- **A:** Focus the guessing map, submit a placed pin and advance through results.
- **B:** Back out of the focused map or pause/return to the game menu as indicated.
- **START:** Pause, regardless of the selected control profile.
- **Select on the game menu:** Open/close the challenge menu.
- **X/Y in the challenge menu:** Change game mode / difficulty.

The app supports customizable button mappings. Look at its current on-screen hints if you use a non-default profile; some older captions elsewhere in the app may still use the default labels.

## Daily and friend challenges

Daily rounds are determined from the console clock interpreted as **UTC**. Players need the same dataset version, difficulty and day for identical locations. Local records include daily bests, recent history, and weekly summaries; no account or online leaderboard is required.

Friend codes contain the dataset identifier, seed, difficulty and an error-detection checksum. They are intended to recreate a game, not to prevent cheating. If an image is unavailable, retry the same image without losing a round; the game does not quietly substitute a different location.

In Pass and Play, everyone uses the same set of five places. Have the other players look away while someone is taking a turn.

## Optional road-geometry pack for Classic

Classic random play normally samples coordinates and checks live imagery. An optional road-geometry file can improve the sampling starting points without bundling Google image data or requiring a paid server.

Place a compatible pack at `sdmc:/3ds_google_maps/streetguess_roads.bin`. To create one from road GeoJSON/NDJSON that you are permitted to use, see `tools/build_guess_roads.py --help`. A full global road pack is **not bundled**, and the normal random sampler remains available without one.

## When a panorama doesn't load

Try again, move to a location with supported imagery, check Wi-Fi and the console clock, or select a different Street View provider for ordinary exploration. In a curated game, a failed panorama can be retried without consuming a round. Classic random mode depends on experimental Google access; curated modes use Panoramax.

Keep the same dataset version when comparing scores with friends. Changing the dataset changes the meaning of existing challenge codes.

## What has been tested?

The app has passed a clean RC3 CIA/3DSX build and its host regression suite. Genuine Azahar footage shows a loaded Panoramax panorama, linked-viewpoint movement, a Daily Challenge pin and result, and live terrain elsewhere in the app. These recordings used a separate capture-only network/clock configuration.

Original 3DS and New 3DS hardware behavior, gyro and stereo responsiveness, and all online providers still need physical-console verification. For the full technical record, see [RC3 verification](release/rc3/verification.md), [image credits](datasets/streetguess-v2.json) and [provider information](release/rc1/provider-review.md).
