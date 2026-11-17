# Audio features

Output devices, normalising, the equaliser and pitch, passthrough, the visualiser, cover art, and the track and subtitle menus. Most of these change one mpv filter chain or one property; see [mpv integration](mpv-Integration.md#filter-graphs).

## Output device

Play > Audio Output > Device lists mpv's `audio-device-list`, showing each device's description, or its name if it has none. The current one is ticked.

- Choosing a device sets mpv's `audio-device` and saves it as `audioDevice`.
- At start-up, `applySettings()` sets `audio-device` again if a device was saved.

Test: `audioOutput`, `audioDevice`. The `audioDevice` check needs mpv to list at least one device and skips otherwise.

## Normalise volume

Play > Audio Output > Normalize Volume (off by default). It adds `lavfi=[dynaudnorm]` to the audio chain and is saved as `audioNormalize`.

## Equaliser

Opened from Play > Audio Output > Equalizer > Open Equalizer...

The dialog has:

| Control | Range | Notes |
|---|---|---|
| Ten vertical sliders | −12 to +12 dB | One per band: 31, 62, 125, 250, 500 Hz, and 1, 2, 4, 8, 16 kHz. |
| Equalizer enabled | | Gates the ten bands. |
| Compressor | | Adds `acompressor`. |
| Stereo widen | | Adds `extrastereo=m=2.5`. |
| Pitch slider | −12 to +12 semitones | See [Pitch](#pitch). |
| Reset | | Sets **every** slider in the dialog to zero, including the pitch slider. |

Each band change saves `eq/gains` and rebuilds the chain at once.

**Presets** in the Equalizer menu (Flat, Bass Boost, Treble Boost, Voice) write the gains and turn the equaliser on:

| Preset | Gains, 31 Hz to 16 kHz |
|---|---|
| Flat | all 0 |
| Bass Boost | 6, 5, 3, 0, 0, 0, 0, 0, 0, 0 |
| Treble Boost | 0, 0, 0, 0, 0, 0, 0, 3, 4, 4 |
| Voice | −2, −2, 0, 0, 2, 3, 4, 2, 0, 0 |

**Band limit:** a band above 45% of the file's sample rate is left out of the chain, because ffmpeg rejects a filter that close to the top of the spectrum. The dialog still shows the slider. The chain is rebuilt on `FILE_LOADED`, once the sample rate is known.

Settings: `eq/enabled`, `eq/gains` (ten strings), `eq/compressor`, `eq/widen`.

Tests: `equalizerDialog`.

## Pitch

The pitch slider (in the equaliser dialog) shifts pitch in semitones, from −12 to +12, and keeps the speed. The chain for a non-zero value is:

```
asetrate=<rate × f>, aresample=<rate>, atempo=<1/f>      where f = 2^(semitones/12)
```

`asetrate` changes the pitch and the speed together. `atempo` changes the speed back. The pitch slider needs a known sample rate, so it has no effect until a file is loaded.

Setting: `eq/pitch` (integer). Test: `pitchShift`.

## S/PDIF passthrough

Play > Audio Output > S/PDIF Passthrough (AC3/DTS). **Off by default.**

When on, Reelplay sends AC3, E-AC3, DTS, DTS-HD and TrueHD to the output without decoding them (`audio-spdif`). This only works when the output is a receiver that can decode them. On a stereo device it gives silence or garbled sound, which is why it is opt-in. The Options page does not have it; it is only in the menu.

Setting: `passthrough`. Test: `passthroughIsOptIn`.

## Visualiser

View > Visualizer (audio files). Off by default; saved as `visualizer`.

The visualiser replaces the video of a file that has none with a spectrum. It is active when all of these are true:

- the setting is on;
- something is loaded (not stopped);
- the file has no video track in mpv's track list.

Then it sets `lavfi-complex` to `[aid1]asplit[ao][v];[v]showspectrum=s=640x360[vo]`. The graph takes the audio track, so while it runs mpv reports `aid` as `no`.

`applyVisualizer()` runs on every `FILE_LOADED` and whenever the menu item is toggled. When the visualiser goes off, it clears the graph **and** selects the first audio and video tracks again by id. Without that, playback stops. See [mpv integration](mpv-Integration.md#track-selection-and-the-visualiser) for the full story.

Limits:

- An MP3 with cover art has a video track (the picture), so the visualiser does not run on it.
- The spectrum is 640 × 360 pixels, drawn in the video area.

Tests: `visualizerForAudioOnly`, `visualiserDoesNotStallPlayback` (the second one is the regression test for the stalled-playback bug).

## Cover art

Options set `audio-display` to `embedded-first`, so a picture embedded in an audio file is shown in place of the visualiser. The test `coverArtForAudio` checks embedded art.

The README also says a `cover.jpg` or `folder.jpg` beside the file works. That comes from mpv's own cover-art handling, not from Reelplay code, and the tests do not check it.

## Audio and subtitle track menus

Play > Audio Track and Play > Subtitle Track are rebuilt from mpv's `track-list` whenever it changes.

- Each entry is labelled `<id>: <title or language or "Track"> [language]`.
- Choosing one sets `aid` or `sid` to its id.
- Subtitle Track starts with "Disabled", which sets `sid` to `no`.
- The ticked item is the one mpv reports as `selected`.

Preferred languages are set in Options > Player: **Preferred audio language** and **Preferred subtitle language**. Each takes a comma-separated list, such as `eng,jpn`, and is passed to mpv as `alang` and `slang`. These only affect which track mpv picks when a file loads.

## Subtitles

- **Auto-load:** `sub-auto` is `fuzzy`, so a subtitle whose name contains the video's name is loaded (for example `film.en.srt` for `film.mkv`). `sub-file-paths` makes mpv also look in `Subs/`, `subs/`, `Subtitles/`, `subtitles/`, `Sub/` and `sub/` next to the video.
- **Load Subtitle...** (Ctrl+L) runs `sub-add <file> select`.
- **Options > Subtitles** sets the font (`QFontComboBox`; saved only if changed), the size (10 to 150, default 55), the colour (default white, chosen with a colour dialog), and the vertical position (0 to 150%, 100 is the bottom).
- **Delay:** F1 and F2 shift subtitles by ±100 ms. See [Playback features](Playback-Features.md#speed-sync-and-volume).

Tests: `subtitleOptions`, `subtitlesFromSubsFolder`.
