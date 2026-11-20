# Languages and ambient music

Open **Settings → Language**. Use the left/right buttons to choose English,
Arabic, French, Spanish, German, Portuguese, Italian, Russian or Turkish.
Language, music and volume preferences are saved to
`sdmc:/3ds_google_maps/language.cfg`. Older language files migrate automatically
and retain their language/music selections, with volume starting at 55%.

The application catalog contains 537 messages for navigation, settings,
StreetGuess, offline downloads, GPS, search, routing and runtime status. Physical
button names and format placeholders are preserved. The catalog is in
`assets/translations/messages.json`; regenerate `source/intl_messages.inc` with
`python3 tools/generate_translations.py`. Some new translations were machine
assisted; community wording review is welcome. Provider/photographer names,
addresses, licenses, system keyboard dialogs and early startup diagnostics are
not translated by the application catalog.

## International place names

A bundled DejaVu Sans font covers extended Latin (including Turkish and
Vietnamese), Greek, Cyrillic, Hebrew, and Arabic/Persian presentation forms.
Arabic letters are joined, lam–alef ligatures are formed, and bidirectional text
is reordered for display. Numbers retain their direction within RTL text. Files
and network requests retain logical UTF-8.

The renderer chooses fonts for individual runs rather than switching an entire
line when CJK appears. Mixed Arabic/CJK/Latin text uses the bundled font where
it has glyphs and the console's regional font for other characters. Both fonts
share a baseline, and centering/right alignment applies to the whole line.
Regional font coverage still depends on the console. This is not universal
Unicode coverage: Indic shaping and emoji are not provided. Downloaded map-tile
labels belong to the imagery provider and are unaffected by app font settings.

Search results, addresses, Favorites, Recents, StreetGuess, and cache statistics
use the graphical Unicode renderer. Choosing Arabic does not install an Arabic
keyboard in Nintendo's separate system keyboard application.

## Music, volume and recovery

Music starts off. Turn **Music** on in the Language tab for an original ambient
loop during Street View Explorer and StreetGuess. It pauses on returning to the
map. Use **− / +** to set volume from 0% to 100%; the setting applies immediately
and is saved. Zero volume mutes playback without starting DSP or synthesizing
an unused buffer. The loop is original, with no Nintendo audio samples.

If DSP initialization or audio-buffer allocation fails, the Language screen
shows a translated error and a **Retry** action. Tapping Retry clears the error
for the next playback attempt. Errors do not retry continuously every frame.
The application remains usable while audio is unavailable.

## Validation

The v2.3 release host gate covers nine locales, 4,833 shaped catalog messages,
774 C format cases, mixed-script font selection, baseline/alignment, preferences
and v1→v2 migration. Audio shim tests cover sample bounds, volume/mute, pause,
resume, DSP/allocation failures, explicit retry and cleanup. The shipped BCFNT's
actual glyph map is checked against all catalog output.

Azahar 2126.2 was used to verify boot, all nine language selections, 0%/100%
volume limits, music toggling, persisted settings, and mixed Arabic/Japanese/
Latin/Cyrillic Favorites. Screenshots are in `docs/media/v2.3.1`.
The test host had no working audio output sink, so audible output and physical
console behavior are not claimed. Network imagery requests also timed out in
this emulator environment; these UI checks do not validate provider availability.

The font asset is `data/unicode_font.bin`; original font and license are in
`assets/fonts`. Regenerate with `python3 tools/build_unicode_font.py` with
`mkbcfnt` on PATH. Font textures occupy about 3 MB. Build dependencies include
`3ds-libfribidi`; host tests need `libfribidi-dev`.
