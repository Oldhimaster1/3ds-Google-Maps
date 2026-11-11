# RC3 promotional media and capture evidence

The current README uses a fast-paced **9.4-second hero GIF**, **20.3-second teaser**, and **32-second showcase**, plus genuine screenshots and separately linked gameplay captures.

## Existing finished edits

- [Hero GIF](../../media/rc3/hero.gif): quick globe, Manhattan and StreetGuess-menu highlights.
- [Teaser](../../media/rc3/teaser.mp4): 20 seconds, short cuts and larger main-screen views.
- [Showcase](../../media/rc3/review-walkthrough.mp4): 32 seconds, maps, globe movement and game menus.
- [Screenshot gallery](../../media/rc3/): real panorama, globe, StreetGuess gameplay and result, terrain, and city maps.

These edited videos were finished before the later online gameplay captures and **do not yet include** panorama viewing, the actual StreetGuess guess/result sequence or live terrain. Do not describe them as a full end-to-end gameplay trailer.

## Newly preserved authentic gameplay

The following **unrepeated source clips are available to watch now**, even without producing another promotional video:

| Clip | What it really shows |
| --- | --- |
| [360° panorama look](../../media/rc3/capture-sources/panorama-look.mp4) | Real Strasbourg Panoramax scene with in-app look controls |
| [Panorama movement](../../media/rc3/capture-sources/panorama-move.mp4) | A verified connected viewpoint and changing scene/minimap |
| [Daily StreetGuess guess](../../media/rc3/capture-sources/daily-guess.mp4) | A real guess, placed pin, distance line and zero-point result |
| [3D terrain](../../media/rc3/capture-sources/terrain.mp4) | Live Grand Canyon Terrarium elevation, orbit and zoom |

Accompanying stills: [panorama](../../media/rc3/panorama.png), [guess](../../media/rc3/streetguess-round.png), [result](../../media/rc3/streetguess-result.png), [terrain](../../media/rc3/terrain-live.png).

These scenes use RC3 application code in Azahar at normal speed. The capture environment required a local proxy CA trusted **only by a diagnostic recording build**, corrected UTC clock, longer network deadlines and temporary request-error logging. HTTPS certificate verification remained on. The actual public RC3 CIA/3DSX files **have not been modified** to add these adjustments. No diagnostic executable or private CA is distributed. See [source capture provenance](../../media/rc3/capture-sources/README.md).

## What remains unavailable

- A verified before/after demonstration of the experimental higher-resolution Google panorama central tile.
- Real 3D buildings; Overpass timed out in the capture environment, and the host request returned HTTP 406.
- Original/New 3DS hardware footage confirming the gyro, stereoscopic effect, Wi-Fi and frame rate.
- A final combined 2–4-minute demonstration containing all headline features. The existing edits are short previews, not that longer video.

These are genuine remaining evidence gaps, not reasons to discard the existing footage. Do not invent buildings, sharpened imagery, scores or additional street views to fill them.

## Media integrity and source notes

The earlier [media manifest](media-manifest.json) records the current hero, teaser and showcase sizes and SHA-256 hashes. The current [editing script](../../../tools/edit_rc3_promo.py) reproduces the existing edits from available source recordings; [edit timing metadata](promo-edit.json) explains the shot selection. The raw clips above are preserved separately.

Credits belong to the original imagery authors: NASA Blue Marble/GIBS, Esri and contributors, Strasbourg Panoramax imagery by Eurométropole de Strasbourg (Etalab 2.0), Sydney Panoramax imagery credited to Leif Metcalf (CC BY-SA 4.0), and Terrarium elevation. The [curated image manifest](../../datasets/streetguess-v2.json) lists individual panorama licenses.

The stable v2.0 release has not been replaced, and no final v2.2.0 GitHub Release has been published.
