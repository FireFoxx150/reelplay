# mpv integration

Reelplay uses libmpv (client API, version 0.37 in CI) for everything that touches media. This page covers how the app configures mpv, talks to it, and listens to it, and the places where mpv behaves in ways that are easy to get wrong.

## Commands and properties

mpv has two ways for a client to act on it, and Reelplay uses both.

| Way | Reelplay helper | Call | Waits for mpv? |
|---|---|---|---|
| Command (`loadfile`, `seek`, `set`, `add`, `cycle`, `stop`, `show-text`, `vf`, `sub-add`, `screenshot-to-file`, `stream-record`, ...) | `Player::command()` | `mpv_command_async` | No. The result is discarded. |
| Command, synchronous | `Player::shutdown()` only | `mpv_command` | Yes. Used for `write-watch-later-config`, which must finish before `mpv_terminate_destroy`. |
| Property read | `Player::prop()` | `mpv_get_property_string` | Yes. |
| Property read and write (numbers) | `Player::step()` | `mpv_get_property`, `mpv_set_property` | Yes. |
| Property write (strings) | `applyAudioFilters()`, `applySettings()` | `mpv_set_property_string` | Yes. |

**The gotcha:** an asynchronous command can finish after the next property read. A `set` followed at once by a read may return the old value. The tests check results with `QTRY_*` macros for this reason. Code that must see a result right away should use a synchronous call.

Property values come back as strings from `prop()`. Anything numeric is converted at the call site with `toInt()` or `toDouble()`. A missing property comes back empty, so an empty string means "not available", not "zero".

## Options set at start-up

The constructor sets these with `mpv_set_option_string` before `mpv_initialize()`. They cannot change once mpv is running.

| Option | Value | Why |
|---|---|---|
| `vo` | `libmpv` (render path only) | Draw into the `VideoWidget` through the render API. See [Video output](#video-output). |
| `wid` | the video widget's native window id (X11 path only) | mpv draws into a native child window. |
| `input-vo-keyboard`, `input-cursor` | `no` (X11 path only) | Qt gets the keys and mouse, not mpv. |
| `idle` | `yes` | mpv stays up with nothing loaded, so the window can show "Stopped" and load files later. |
| `input-default-bindings` | `no` | mpv's own key bindings are off; Reelplay provides every shortcut. |
| `osc` | `no` | mpv's on-screen controller is off; Reelplay has its own controls. |
| `sub-auto` | `fuzzy` | Load subtitles whose name contains the video's name, as MPC-BE does. |
| `sub-file-paths` | `Subs:subs:Subtitles:subtitles:Sub:sub` | Look for subtitles in these folders next to the video. |
| `ao` | from `REELPLAY_AO`, if set | `null` for no audio device (CI). |
| `vo` | from `REELPLAY_VO`, if set | `null` for no video output (CI). Overrides the render-path `vo` when set. |
| `audio-display` | `embedded-first` | Show an audio file's embedded cover art. |
| `ytdl` | `yes` | Let mpv call yt-dlp for web pages such as YouTube. |
| `watch-later-dir` | `<AppData>/watch_later` | Keep resume files apart from the mpv command-line player's. |
| `watch-later-options` | `start,aid,sid,sub-delay,audio-delay` | What a resume file remembers. |
| `loop-file` | `inf` for Repeat File, otherwise `no` | Repeat File is done by mpv, not by the app. |

After `mpv_initialize()`, a failure is fatal: the app calls `qFatal`.

`main()` sets `LC_NUMERIC` to `C` before any of this. libmpv parses numbers in options and properties with the C locale, so a decimal-comma locale would break them.

## Runtime settings

`Player::applySettings()` runs at start-up and after the Options dialog closes. It pushes every saved setting into mpv as a property, so mpv always matches the saved settings.

| mpv property | Source setting | Default |
|---|---|---|
| `hwdec` | `hwdec` | `no` (software decoding) |
| `alang`, `slang` | `alang`, `slang` | empty |
| `sub-font` | `subFont` | Not set unless the user picks a font |
| `sub-font-size` | `subSize` | 55 |
| `sub-color` | `subColor` | `#ffffff` |
| `sub-pos` | `subPos` | 100 (bottom) |
| `brightness`, `contrast`, `saturation`, `gamma`, `hue` | `video/<name>` | 0 |
| `audio-device` | `audioDevice` | Not set (mpv's default device) |
| `af` | `audioNormalize`, `eq/*` | Empty chain. See [Filter graphs](#filter-graphs). |
| `scale` | `scale` | `bilinear` |
| `tone-mapping` | `toneMap` | `auto` (on) |
| `audio-spdif` | `passthrough` | Empty (off) |
| `vf` labels `@flip`, `@denoise`, `@sharpen` | `video/flip`, `video/denoise`, `video/sharpen` | Off |
| `deinterlace` | `video/deinterlace` | `no` |
| `video-rotate` | `video/rotate` | 0 |

`hwdec` offers `no`, `auto-safe`, `vaapi`, `nvdec` and `vdpau`. Hardware decoding is off by default, so a fresh install decodes in software.

## Observed properties

`mpv_observe_property` tells mpv to send `MPV_EVENT_PROPERTY_CHANGE` whenever one of these changes.

| Property | Format | Reaction |
|---|---|---|
| `time-pos` | double | Updates the time label and the seek bar, unless the user is dragging the bar. |
| `duration` | double | Sets the seek bar range (tenths of a second) and the chapter ticks; enables the seek bar. |
| `pause` | flag | Status label shows Paused or Playing. |
| `volume` | double | Moves the volume slider, with its signal blocked so it doesn't echo back. |
| `mute` | flag | Sets the check state of the Mute action. |
| `track-list` | none | Rebuilds the Audio Track and Subtitle Track menus. The event has no data; the app reads the list itself. |
| `chapter-list` | none | Re-reads the chapter list. The event has no data; the app reads the list itself. |

Properties observed with `MPV_FORMAT_NONE` arrive without a value. `handleEvents()` checks for them after the two list properties and skips the rest.

## Events

`handleEvents()` handles these event types. The rest are ignored.

| Event | What Reelplay does |
|---|---|
| `MPV_EVENT_PROPERTY_CHANGE` | See [Observed properties](#observed-properties). |
| `MPV_EVENT_START_FILE` | Status label: "Opening...". |
| `MPV_EVENT_FILE_LOADED` | Status label becomes Playing or Paused. Stores the file's size. Recomputes the audio filter chain (the equaliser band limits depend on the sample rate). Applies the visualiser. Re-applies the saved crop. Performs any pending favourite seek. |
| `MPV_EVENT_END_FILE` | Advances, repeats, stops or runs the After Playback action. See [Playback and files](Playback-and-Files.md#end-of-file). |
| `MPV_EVENT_PLAYBACK_RESTART` | Emits the MPRIS `Seeked` signal with the new position. |
| `MPV_EVENT_SHUTDOWN` | Closes the window. |

## Video output

Two paths, chosen in the constructor:

| Path | When | How |
|---|---|---|
| **X11 embedding** (default on X11) | `QGuiApplication::platformName() != "wayland"` and `REELPLAY_RENDER` is not `gl` | The video area is a plain `QWidget` with a native window (`WA_NativeWindow`). mpv draws into it through `wid`. |
| **Render API** | Wayland, or `REELPLAY_RENDER=gl` | `VideoWidget` (a `QOpenGLWidget`) creates an `mpv_render_context` with OpenGL, and paints each frame in `paintGL()`. |

The reason for embedding on X11 is start-up time. The constructor comment records that a `QOpenGLWidget` costs 0.5 to 1 s of GL setup before the window appears (measured on NVIDIA). Wayland cannot embed another process's window, so it has to use the render API.

Consequences of embedding:

- Always-on-top is set on the `QWindow`, not the widget. Recreating the widget's window would orphan mpv's native window. See [Video features](Video-Features.md#window).
- Keys and mouse go to Qt, because `input-vo-keyboard` is `no`.
- Qt does not paint over the video area, so it is given a black background stylesheet.

The render path has two rules:

- `MPV_RENDER_PARAM_ADVANCED_CONTROL` is **not** set. With it, mpv forbids synchronous calls from the render thread, which here is the GUI thread, and calling them deadlocks.
- The update callback runs on an mpv thread and only queues a call to the GUI thread. Freeing the context must happen before the GL context is destroyed. `VideoWidget::release()` does this with the GL context made current.

## Filter graphs

mpv has three filter slots that Reelplay uses. They are different things.

### `af`: the audio chain

`applyAudioFilters()` builds the whole `af` string every time it runs. It is set as one value, so anything not in the string is removed. The filters, in order:

1. `lavfi=[dynaudnorm]` when normalising is on.
2. One `lavfi=[equalizer=f=<Hz>:t=o:w=1:g=<dB>]` per band that has a non-zero gain. Bands above 45% of the file's sample rate are skipped, because ffmpeg rejects a band that close to or above the Nyquist limit.
3. `lavfi=[acompressor]` when the compressor is on.
4. `lavfi=[extrastereo=m=2.5]` when stereo widen is on.
5. Pitch shift when the semitone setting is not zero: `lavfi=[asetrate=<rate*f>,aresample=<rate>,atempo=<1/f>]`, where `f = 2^(semitones/12)`. The tempo change puts the speed back, so only the pitch moves.

### `vf`: labelled video filters

Flip, denoise and sharpen are labelled so they can be added and removed without stacking:

```
vf remove @denoise
vf add @denoise:hqdn3d     (only if turned on)
```

`setVideoFilter()` always removes first, so calling it with the same state twice is harmless. `applySettings()` depends on this when it re-applies everything.

### `lavfi-complex`: the visualiser

The visualiser replaces the whole filter graph with `[aid1]asplit[ao][v];[v]showspectrum=s=640x360[vo]`. This graph takes the audio track, which is why the track handling below matters.

Other video settings are plain properties rather than filters: `video-rotate`, `deinterlace`, `tone-mapping`, `scale`, `video-crop`, `video-aspect-override`, and the five colour properties.

## Track selection and the visualiser

While `lavfi-complex` consumes the audio track, mpv reports `aid` as `no`. Clearing the graph does **not** give the track back. The result, before the fix: playback stopped (the clock froze and the playlist never advanced), and when the file had video, the picture stayed black.

The fix in `applyVisualizer()` (commit `aac1739`): when a graph that was active is cleared and the visualiser is not wanted, select the first audio track and the first video track again, by id:

```cpp
set aid <id of first audio track>
set vid <id of first video track>
```

Two other fixes were tried and failed, so they are worth knowing about:

- `set aid auto`: playback did not resume.
- Setting only the audio id: playback resumed, but the video picture stayed black.

This is a **workaround**. The root cause is in how mpv handles the track after the graph is removed. Upstream behaviour has not been checked.

Related: an attached picture in an audio file (cover art) is a video track in mpv's track list. The visualiser therefore does not run on an MP3 with cover art. This is intended, because the file has a picture to show.

## Resume (watch-later)

mpv writes one resume file per media item in `watch-later-dir`. Reelplay makes sure this happens at three points:

- when a different file starts (`savePosition()` in `playIndex()`);
- when the user chooses File > Close (`savePosition()`);
- on a clean exit, while something is loaded (`shutdown()`).

A crash, or a process killed with a signal, writes nothing. The position from the last of those points is what comes back next time.

The resume file records the start position, the audio track, the subtitle track, and the subtitle and audio delays (`watch-later-options`).

## Stream recording

`startRecording()` sets `stream-record` to the chosen path. mpv copies the stream packets without re-encoding, and writes the file when recording stops. The stop happens when the user unticks the item, or when another file starts (`playIndex()` stops a running recording, so a recording follows one file).

Setting `stream-record` is asynchronous, like every `command()`. The test `recordStream` therefore uses `QTRY_COMPARE` to read it back. See [Testing](Testing.md#known-sensitive-spots).

## Debugging mpv

The app does not ask mpv for log messages. To see them temporarily:

1. Call `mpv_request_log_messages(mpv, "info")` after `mpv_create()`.
2. Handle `MPV_EVENT_LOG_MESSAGE` in `handleEvents()` and print the text.
3. Remove both before committing.

The visualiser investigation did this in a scratch copy of the sources, not in the repository.

To check what mpv thinks is loaded, read the track and file properties with `prop()`. For example, `track-list/<n>/type`, `track-list/<n>/selected`, `aid`, `vid`, `lavfi-complex` and `af`. In the test suite, the same reads are done through `p->prop(...)`.

## Shutdown

The order is part of the contract. See [Architecture](Architecture.md#shutdown-sequence). In short: write the resume file synchronously, free the render context, then `mpv_terminate_destroy()`.
