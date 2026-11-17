# Languages and ambient music

Open **Settings → Language**. Use the left/right buttons to choose English,
Arabic, French, Spanish, German, Portuguese, Italian, Russian or Turkish.
The language selector shows each language's native name. Language and music
preferences are saved to `sdmc:/3ds_google_maps/language.cfg`.

The core navigation, settings labels and StreetGuess menu controls have
translations. Advanced status messages, some game instructions and legacy
console diagnostics currently fall back to English. Translations are stored in
`source/intl.c`; untranslated strings retain their original English text.
Community review of wording and small-screen layout is welcome.

## International place names

The app bundles a DejaVu Sans font instead of relying solely on the console's
regional font. It covers extended Latin (including Turkish and Vietnamese),
Greek, Cyrillic, Hebrew, and Arabic/Persian presentation forms. Arabic is joined,
lam–alef ligatures are formed, and bidirectional text is reordered for display.
Numbers retain their direction inside Arabic and Hebrew names. Text in files
and network requests stays in its original logical UTF-8 order.

Search results, identified addresses, Favorites, Recents, and the graphical
StreetGuess interface use this renderer. Japanese, Chinese and Korean strings
continue to use the console's regional font, whose coverage depends on the
console. This is not universal Unicode coverage: Indic scripts, emoji and
mixed Arabic/CJK strings need additional fonts and shaping work. Map labels
inside downloaded image tiles belong to the tile provider and are not changed
by the application's font or menu language.

The Nintendo system keyboard is a separate system application. Choosing Arabic
does not add an Arabic keyboard to a console whose keyboard lacks it.

## Music

Music is off initially. Turn **Music** on in the Language tab to hear an original,
quiet ambient chord loop during Street View Explorer and StreetGuess. It pauses
when returning to the map and resumes when entering those modes again. It is
not the Wii U Street View soundtrack.

The loop is synthesized locally; no network connection or audio download is
needed. It uses a 256 KB PCM buffer and NDSP channel 23. Homebrew environments
without working DSP support continue silently. Real-console audio, suspend and
resume, and translated layouts still need hardware verification.

## Development checks

Install `3ds-libfribidi` along with the existing devkitPro dependencies. Host
regressions additionally need `libfribidi-dev`. Run `sh tests/run-all-host.sh`;
`run-intl-host.sh` checks Arabic ligatures, mixed-direction numbers, Hebrew,
UTF-8, translated locale labels and persistence. `run-ambient-music-host.sh`
checks audio samples, pause/resume, failure paths and cleanup with an NDSP shim.
Neither host shim validates console rendering or audible output.

`data/unicode_font.bin` is a generated BCFNT asset. The original font and license
are in `assets/fonts`; regenerate with `python3 tools/build_unicode_font.py`
with devkitPro's `mkbcfnt` on PATH. The font's codepoint whitelist keeps its
texture footprint near 3 MB for original 3DS memory budgets.
