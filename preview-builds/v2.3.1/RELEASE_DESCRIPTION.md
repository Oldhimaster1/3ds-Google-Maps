## What’s new

- **Nine interface languages**, including Arabic, with 537 translated messages, joined Arabic letters and mixed-script font fallback.
- **Optional original ambient music** for Street View and StreetGuess, with saved 0–100% volume, mute, visible audio errors and retry.
- **Expanded StreetGuess:** 113 curated panoramas across 33 countries, with photographer credits and licenses.
- **Progressive panorama detail:** load up to four visible high-resolution tiles as you zoom, using about 2 MiB for resident detail textures instead of assembling a full HD panorama in RAM.

## Downloads and installation

- **CIA:** install `3ds_google_maps.cia` using FBI.
- **Homebrew Launcher:** copy `3ds_google_maps.3dsx` to `sdmc:/3ds/3ds_google_maps/3ds_google_maps.3dsx`.
- Verify downloads using `SHA256SUMS`. Back up application data before upgrading.

## Testing and known limits

35 host suite entries passed, including 4,833 shaped translations and 774 translated format cases. CIA installation, all nine language selections, volume limits, music toggling and saved settings were checked in Azahar 2126.2.

Physical-console audio and performance remain unverified. Live imagery requests timed out in the emulator test environment, so those tests do not establish provider availability. Tiled HD currently applies to the experimental Google provider; Panoramax rounds retain SD imagery. System keyboards and map-image labels are separate from the app’s translations. Friend codes require matching dataset versions.

Source commit: `594182424e952ecb8f7a2bd0e96fc4d3ca3155e2`.
