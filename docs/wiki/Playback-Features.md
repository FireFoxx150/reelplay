# Playback features

The features that work on what is playing, rather than on how it looks or sounds: the seek bar, previews, A-B repeat, section saving, screenshots, recording, chapters, bookmarks, favourites, resume, speed and sync, repeat, and file information. Each entry says where the code is and how it works.

## Seek bar

`seekBar` is a `ChapterSlider`, a `QSlider` with extra tick marks.

- **Range:** 0 to `duration × 10`. The unit is a tenth of a second, so a two-hour file has a range of 72,000.
- **Ticks:** `updateChapterMarks()` converts each chapter time to the same units and paints a short mark above and below the groove.
- **Clicking** jumps straight to the clicked point. `JumpStyle` makes the left button do this, where Qt's default would step by a page.
- **Dragging** sends `seek <value> absolute` as the handle moves. While the user holds the handle, `updateTime()` does not move it, so playback doesn't fight the drag.
- The bar is disabled until a file with a known duration is loaded.

## Frame previews

Hovering over the seek bar shows the time in a tooltip and a small frame above the bar.

| Step | Detail |
|---|---|
| When | Only for a local file (the source must be a file on disk). Streams get the tooltip only. |
| Debounce | 150 ms after the pointer stops, so sweeping the bar doesn't start a process for each step. |
| Unit | Whole seconds. The cache key is the second. |
| Command | `ffmpeg -v error -ss <s> -i <file> -frames:v 1 -vf scale=160:-2 -f image2pipe -vcodec png -` |
| Output | A PNG, read from the process's stdout. |
| Superseded requests | A new request kills the one running. Results for a second the user has moved past are discarded. |
| Cache | Kept per file. Cleared when the file changes. When it passes 300 entries it is cleared completely. |

Without ffmpeg installed there are no frames, but the tooltip still works. The test `seekPreviewFrames` skips when ffmpeg is missing.

## Go To

Ctrl+G, or Play > Go To. The dialog starts with the current time. It accepts:

- `hh:mm:ss`, `mm:ss` or seconds, with decimals;
- `parseTime()` returns -1 for anything else, and the status bar shows `Invalid time: ...`.

Does nothing when stopped.

## A-B repeat

The A-B Repeat submenu sets mpv's `ab-loop-a` and `ab-loop-b`:

| Key | Action |
|---|---|
| `[` | Set marker A to the current position. |
| `]` | Set marker B to the current position. |
| `\` | Clear both. |

mpv does the looping. The markers are per session and are not saved. An OSD message confirms each change.

## Save Section

Play > A-B Repeat > Save Section.

- Needs a loaded local file, both markers set, and B after A. Otherwise it shows an OSD message and does nothing.
- The save dialog suggests `<name>_section.mkv`. The filter offers `.mkv` and `.mp4`.
- Runs `ffmpeg -v error -y -ss <A> -i <src> -t <B−A> -c copy -avoid_negative_ts make_zero <out>`, as a child process.
- `-c copy` copies the streams without re-encoding, so it is fast. The cut can only start on a keyframe, so the start is the keyframe at or before A, and the section can begin a little early. The code comment says so, and the README says so too.
- The OSD shows success, failure, or "Could not run ffmpeg".

The test `abSectionSave` skips when ffmpeg is missing.

## Screenshots

| Action | Shortcut | Result |
|---|---|---|
| Save Image (Auto) | F5 | Saves to the automatic path. |
| Save Image... | Alt+I | Asks where to save (PNG or JPEG). |
| Screenshot Series... | File menu | Saves every N seconds, until unticked. |

**Automatic name:** `<file base name>_<position hh.mm.ss>_<wall-clock hhmmsszzz>.png`, in the Screenshot folder set in Options, or `~/Pictures/reelplay` if none is set. The position and the clock time keep the names unique.

**Method:** mpv's `screenshot-to-file <path> video`. The `video` mode takes the video frame without subtitles or the on-screen display. This does **not** use ffmpeg.

**Series:** the timer keeps running across file changes. It checks `!stopped` on each tick, so it takes no screenshots while nothing is playing. Interval range 1 to 3600 seconds, default 10.

Tests: `autoScreenshot`, `screenshotSeries`.

## Stream recording (experimental)

Play > Record Stream (Experimental). The save dialog offers `.mkv`, `.mp4`, `.ts` and `.webm`.

- Sets mpv's `stream-record` property. mpv copies the stream packets without re-encoding.
- The file is written when recording stops: when the item is unticked, or when another file starts.
- Starting a recording part way through can begin with frames that cannot be decoded. The code comment and the README both warn about this.
- mpv marks this feature experimental, and Reelplay does too.

Test: `recordStream`.

## Chapters

- `readChapters()` runs when mpv's `chapter-list` changes. It stores the start times in `chapters`.
- Play > Chapters lists them as `n. title (time)`. Untitled chapters are shown as `Chapter n`. The current chapter is ticked.
- Next Chapter (Ctrl+PgDn) and Previous Chapter (Ctrl+PgUp) add ±1 to mpv's `chapter`.
- **Skip Intro / Credits (S):** `skipIntro()` matches the current chapter's title against `intro|opening|recap|credit|ending` (case-insensitive).
  - If it matches and there is a next chapter, jump to it.
  - If it matches and it is the last chapter, seek to 100%, which runs the credits to the end.
  - If it does not match, the OSD says "Not an intro or credits chapter".

Tests: `chapters`, `skipIntroChapter`. The latter needs ffmpeg to write chapter titles, and skips without it.

## Bookmarks

- Ctrl+B, or Play > Bookmarks > Add Bookmark, stores the current time and the source as `secs<TAB>source` in the `bookmarks` setting.
- The list keeps the newest 500 entries, across all files.
- Play > Bookmarks shows only the current file's entries. Choosing one seeks to it.
- Clear Bookmarks for This File removes this file's entries and keeps the rest.

Test: `bookmarksPerFile`.

## Favourites

- Ctrl+Shift+F, or Favorites > Add Current File, stores the current time and source as `secs<TAB>source` in `favorites`.
- There is **one entry per file**. Adding again replaces its time and moves it to the top. The list keeps 30 entries.
- Choosing an entry opens the file and sets `pendingSeek`. The seek is done on `FILE_LOADED`, once the file can be seeked.
- Clear Favorites empties the setting.

Test: `favorites`.

## Resume

Every file resumes where it was left, through mpv's watch-later files. The details, including when they are written and what is remembered, are in [mpv integration](mpv-Integration.md#resume-watch-later). Test: `resumePosition`.

## Speed, sync and volume

| Control | Keys | Step | Notes |
|---|---|---|---|
| Playback rate | Ctrl+Up, Ctrl+Down, Ctrl+R | ±0.1, from 0.25 to 4; Ctrl+R resets to 1 | `step()` is synchronous, so repeated keys never lose a step. |
| Subtitle delay | F1, F2 | ±100 ms | Stored by mpv, and remembered in resume files. |
| Audio delay | Numpad − and + | ±10 ms | As above. |
| Volume | Up, Down; mouse wheel over the video | ±5 | The volume slider is saved on close. |
| Mute | Ctrl+M | Toggle | |

Each change shows the new value in an OSD message. MPRIS uses the same rate range (0.25 to 4) and ignores zero or negative rates.

## Repeat

See [Playback and files](Playback-and-Files.md#repeat).

## File information

- **File > Properties** (Shift+F10) shows a dialog built by `propertiesText()`: file name, path, size, duration, container, video codec, size, frame rate, bitrate, audio codec, sample rate, channels, and bitrate. Missing values show as `n/a`.
- **Ctrl+J** turns on the statistics overlay. `updateStats()` runs every 500 ms and shows the video and audio codec, the size, the frame rate, the hardware decoder in use, dropped frames, A/V sync, and the bitrate.

Tests: `propertiesDialogText`, `statsOverlay`.

## Zoom

Alt+1, Alt+2, Alt+3 set the window to 50%, 100% or 200% of the video's size, plus the size of the window's chrome (menu bar, controls, status bar). Zoom does nothing in fullscreen, or before the video size is known. It leaves the maximised state first.
