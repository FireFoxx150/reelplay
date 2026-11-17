# Settings and data

Where Reelplay keeps its settings and data, every key it saves, and how the old `newmpc-be` data is moved over. Settings use `QSettings` with the organisation and application name `reelplay`, so the defaults are per-user files.

## Where things live

| What | Path | Notes |
|---|---|---|
| Settings | `~/.config/reelplay/reelplay.conf` | INI format. Keys with `/` become groups, so `video/brightness` is `brightness` under `[video]`. |
| Resume files | `~/.local/share/reelplay/watch_later/` | One file per media item, written by mpv. |
| Screenshots | `~/Pictures/reelplay/` | Unless `screenshotDir` is set. |

Tests change these locations. `QStandardPaths::setTestModeEnabled(true)` is set in `initTestCase()`, and `init()` clears the settings and the watch-later folder before each test. See [Testing](Testing.md#isolation).

## Settings keys

Every key the code reads or writes, with its default and where it is set. "Options" means the Options dialog.

### Player

| Key | Type | Default | Set by | Meaning |
|---|---|---|---|---|
| `hwdec` | string | `no` | Options | Hardware decoding mode passed to mpv. |
| `alang` | string | empty | Options | Preferred audio languages, comma-separated. |
| `slang` | string | empty | Options | Preferred subtitle languages, comma-separated. |
| `screenshotDir` | string | Pictures folder | Options | Where automatic screenshots go. Removed when left empty. |
| `darkTheme` | bool | `true` | Options | Dark palette with the Fusion style. |
| `singleInstance` | bool | `true` | Options | Second launch hands its files to the running window. Read at launch. |
| `autoNext` | bool | `false` | Options | At the end of a lone file, play the next file in its folder. |
| `afterPlayback` | string | `nothing` | Play menu | `nothing`, `exit`, `poweroff` or `suspend`. |
| `repeat` | int | `0` | Play menu | `0` off, `1` repeat file, `2` repeat playlist. |
| `volume` | int | `100` | On close | Volume, 0 to 100. |
| `geometry` | bytes | none | On close | Window geometry (`saveGeometry()`). |
| `windowState` | bytes | none | On close | Dock and toolbar state (`saveState()`). The playlist dock is named `playlist` for this. |

### Subtitles

| Key | Type | Default | Set by | Meaning |
|---|---|---|---|---|
| `subFont` | string | not set | Options | Font family. Saved only when changed, so mpv's default is kept otherwise. |
| `subSize` | int | `55` | Options | Size, 10 to 150. |
| `subColor` | string | `#ffffff` | Options | Colour as `#rrggbb`. |
| `subPos` | int | `100` | Options | Vertical position, 0 to 150 (100 is the bottom). |

### Video

| Key | Type | Default | Set by | Meaning |
|---|---|---|---|---|
| `video/brightness`, `video/contrast`, `video/saturation`, `video/gamma`, `video/hue` | int | `0` | Adjust Colors | −100 to 100. |
| `video/rotate` | int | `0` | Rotate menu | 0 to 359. |
| `video/flip` | bool | `false` | View > Video | Horizontal flip. |
| `video/deinterlace` | bool | `false` | View > Video | Deinterlace. |
| `video/denoise` | bool | `false` | View > Video | Denoise filter. |
| `video/sharpen` | bool | `false` | View > Video | Sharpen filter. |
| `video/crop` | double | `0` | Crop menu | Crop aspect ratio. `0` for no crop. |
| `scale` | string | `bilinear` | View > Video > Scaling | `bilinear`, `ewa_lanczossharp` or `spline36`. |
| `toneMap` | bool | `true` | View > Video | HDR tone mapping. |
| `visualizer` | bool | `false` | View | Spectrum for audio files. |

### Audio

| Key | Type | Default | Set by | Meaning |
|---|---|---|---|---|
| `audioDevice` | string | not set | Audio Output > Device | mpv's device name. |
| `audioNormalize` | bool | `false` | Audio Output | Normalise volume. |
| `passthrough` | bool | `false` | Audio Output | S/PDIF passthrough. |
| `eq/enabled` | bool | `false` | Equaliser | Band gains are applied. |
| `eq/gains` | string list | ten zeros | Equaliser | Band gains in dB, from 31 Hz to 16 kHz. |
| `eq/compressor` | bool | `false` | Equaliser | Compressor. |
| `eq/widen` | bool | `false` | Equaliser | Stereo widen. |
| `eq/pitch` | int | `0` | Equaliser | Pitch in semitones, −12 to 12. |

### Files and history

| Key | Type | Default | Set by | Meaning |
|---|---|---|---|---|
| `recent` | string list | empty | Each file played | Up to 10 recent sources, newest first. |
| `bookmarks` | string list | empty | Ctrl+B | `seconds<TAB>source`, up to 500, newest first. |
| `favorites` | string list | empty | Ctrl+Shift+F | `seconds<TAB>source`, up to 30, one per file. |

### Hotkeys

| Key | Type | Meaning |
|---|---|---|
| `keys/<Menu>/<Item>` | key sequence | A remapped hotkey. For example `keys/Play/Play-Pause`. Written only when different from the default. |

Playlist rows keep their data in memory only, in `Qt::UserRole` and the roles after it. Nothing about the playlist is saved except through the M3U export.

## Writing a new setting

The defaults are written at each place the setting is read, not in one table. For example, `toneMap` defaults to `true` in both the menu constructor and `applySettings()`. Keep those in step, or the menu and the player will disagree on a first launch. See [Contributing](Contributing.md#adding-a-setting).

## Migration from newmpc-be

Reelplay was called `newmpc-be` up to version 0.3.1. `migrateOldName()` in `main.cpp` runs at every start and moves the old data over, once:

- `~/.config/newmpc-be/` becomes `~/.config/reelplay/`, and its `newmpc-be.conf` is renamed to `reelplay.conf`. This happens only if `~/.config/reelplay/` does not exist yet.
- `~/.local/share/newmpc-be/` becomes `~/.local/share/reelplay/`, under the same rule.

It never overwrites existing Reelplay data. The Debian package replaces and conflicts with `newmpc-be`, so the old package is removed on install.
