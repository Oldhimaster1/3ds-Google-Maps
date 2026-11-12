# GPS maintenance production publication checklist

The source repairs and permanent FBI QR are prepared on this PR branch. **This change does not replace the published v2.2.0 CIA/3DSX assets.**

1. Review and merge this pull request into main; preserve historical release tags and preview builds.
2. Open [v2.2.0 on GitHub](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/tag/v2.2.0), edit the release, and replace the old CIA and 3DSX assets with the exact files from the [GPS reliability preview](../../preview-builds/gps-session-reliability-20261112/). Preserve the filenames `3ds_google_maps.cia` and `3ds_google_maps.3dsx`.
3. Add a short note that updated binaries were compiled from source commit `c015006a515c339481b29fb2db3963b808b75e49`, since the existing v2.2.0 tag predates these repairs. Preserve the CIA title ID `000400000FD34B00`.
4. Download both replacements from the *public GitHub release*, and independently compare SHA-256 with [GPS maintenance build checksums](../releases/gps-maintenance-SHA256SUMS): CIA `999b793727984a388bbab497bdf8baf8c437406161dc4e93d730677727e22b3a`; 3DSX `cd9b19f6bacc5a07503050e237f50dea48487ce1d823261b91532f7327725fb4`.
5. Check [latest CIA](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/latest/download/3ds_google_maps.cia) and [latest 3DSX](https://github.com/Oldhimaster1/3ds-Google-Maps/releases/latest/download/3ds_google_maps.3dsx). They follow the current stable GitHub release; **before the manual asset replacement they still install the OLD published binaries**.
6. Verify the public [permanent FBI QR](../media/fbi-install-latest.png) scans and installs the replacement CIA on real 3DS hardware. The QR encodes `https://github.com/Oldhimaster1/3ds-Google-Maps/releases/latest/download/3ds_google_maps.cia`; its pixels were decoded with OpenCV prior to committing. GitHub release redirects should also be verified on FBI; until that succeeds, the existing [working, pinned GPS preview QR](../../preview-builds/gps-session-reliability-20261112/fbi-install.png) provides a fallback.
7. Confirm the README, release notes, checksum and assets match and announce a small GPS reliability maintenance update if desired.

The **public FBI installation QR** is stable and can be shared. The **in-app GPS pairing QR** is intentionally a private, per-session credential and must *not* be replaced with the public FBI QR. Old preview QR codes remain pinned to their historical packages and are intentionally not overwritten.
