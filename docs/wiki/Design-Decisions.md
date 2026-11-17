# Design decisions

Why the code is shaped the way it is. Each entry gives the decision, the reason (from the code comments, the README, or the history, and marked as inferred where it is not written down), and the cost.

## Playback

### libmpv does the media work

**Decision:** all decoding, demuxing, rendering, track selection and disc handling go through libmpv.

**Why:** the README's goal is that Reelplay plays what VLC plays. mpv covers the formats, discs, streams and subtitle handling that goal needs, and Reelplay does not reimplement them.

**Cost:** mpv's quirks become Reelplay's quirks. The visualiser's track problem is one. See [mpv integration](mpv-Integration.md).

### Resume state is mpv's watch-later files

**Decision:** resume positions are stored by mpv, in a folder of their own, rather than in a Reelplay database.

**Why:** a comment in `player.cpp` gives the reason: "our own dir so we don't mix with the mpv CLI". mpv already writes and reads the files.

**Cost:** the files are written only at three points, so a crash loses the position since the last one. See [Playback features](Playback-Features.md#resume).

### Stream copy for section saving

**Decision:** Save Section uses `ffmpeg -c copy`, with no re-encoding.

**Why:** the code comment says the cut "starts on the keyframe at or before A". Stream copy is fast and loses no quality, which suits a quick save.

**Cost:** the section can start early. Exact cuts would need re-encoding, which the code does not offer.

### Previews and cuts use the ffmpeg command line

**Decision:** frame previews and section cuts run `ffmpeg` as a child process, rather than linking libavcodec.

**Why:** the code does not say. Inferred: it keeps the build simple, and ffmpeg is already a runtime dependency for the other features.

**Cost:** one process per preview, and a runtime dependency that is optional but needed for those features.

### Stream recording is marked experimental

**Decision:** Record Stream is labelled experimental in the menu and the README.

**Why:** mpv marks its `stream-record` option experimental, and a recording started part way through can begin with undecodable frames. The code comment says so.

**Cost:** users may find it unreliable. The label is the warning.

## Video

### Embed mpv in a window on X11

**Decision:** on X11, mpv draws into a native child window of the video widget (`wid`).

**Why:** a comment in the constructor records that a `QOpenGLWidget` costs 0.5 to 1 s of GL setup before the window appears, measured on NVIDIA. Embedding avoids that.

**Cost:** there are two video paths to keep working. Always-on-top has to be set on the `QWindow`, not the widget, because recreating the widget's window would orphan mpv's window (a comment in `setAlwaysOnTop()` says so).

### The render API on Wayland

**Decision:** on Wayland, and when `REELPLAY_RENDER=gl` is set, `VideoWidget` draws mpv's frames through the render API.

**Why:** a comment in the constructor says Wayland "can't embed another process's window".

**Cost:** this path is not tested in a real Wayland session. See [Troubleshooting](Troubleshooting.md#wayland).

### No `ADVANCED_CONTROL` on the render context

**Decision:** the render context is created without `MPV_RENDER_PARAM_ADVANCED_CONTROL`.

**Why:** a comment in `VideoWidget::initializeGL()` says that flag "forbids synchronous mpv calls on the render thread", which here is the GUI thread, and "deadlocks if broken".

**Cost:** mpv's own render thread does more of the work, which the app does not control.

### Labelled video filters

**Decision:** flip, denoise and sharpen are added as labelled filters (`vf add @label:filter`), and removed by label before they are added again.

**Why:** a comment on `setVideoFilter()` says that reapplying saved settings "never stacks a second copy of the filter".

**Cost:** none that matters. The label is a small amount of extra syntax.

## Shutdown and threads

### Ordered teardown

**Decision:** `shutdown()` writes the resume file synchronously, frees the render context, then destroys mpv. `closeEvent()` leaves fullscreen and saves the layout before that.

**Why:** the comment above `Player::shutdown()` says that the wrong order makes mpv's video thread hit "a dead X window", and that Xlib then aborts the process. Leaving fullscreen first keeps the saved geometry windowed.

**Cost:** the order is fragile, and a comment is the only protection. See [Architecture](Architecture.md#shutdown-sequence).

### Work on the GUI thread, with queued callbacks from mpv

**Decision:** all Reelplay logic runs on the GUI thread. mpv's callbacks only post a queued call.

**Why:** Qt widgets must be touched from one thread. The callbacks come from mpv's threads, and `QMetaObject::invokeMethod` with `Qt::QueuedConnection` moves the work across.

**Cost:** a slow handler delays the window. The handlers are short.

## Desktop

### A single window over a well-known D-Bus name

**Decision:** a second launch registers nothing; it calls `openFromOtherInstance()` on the first window's name.

**Why:** a comment in `main()` says `registerService()` is "atomic, so no race": two launches cannot both become the window.

**Cost:** it needs a session bus. The five-second wait is the only protection against a hung window, and a slow one causes a second window.

### Grabbing media keys from the settings daemons as well as MPRIS

**Decision:** Reelplay calls `GrabMediaPlayerKeys` on the GNOME, Cinnamon and MATE settings daemons, as well as exporting MPRIS.

**Why:** a comment in `mpris.h` says those desktops "fall back to MPRIS" and then "pick the first player that ever registered, e.g. a browser". Grabbing the keys makes the window the one that gets them.

**Cost:** the most recently activated Reelplay window wins the keys, which can surprise someone who has two windows open. Volume keys are left alone.

## Audio

### Passthrough is off by default

**Decision:** S/PDIF passthrough is an opt-in menu item, saved as `passthrough`, default off.

**Why:** a comment in `setPassthrough()` says a device that cannot take the formats "gives silence or garbled sound".

**Cost:** users who have a receiver must find the menu item.

### Equaliser bands above 45% of the sample rate are skipped

**Decision:** a band above 45% of the file's sample rate is left out of the filter chain.

**Why:** a comment on `applyAudioFilters()` says ffmpeg rejects those bands.

**Cost:** the slider still shows, and it does nothing on that file. The rate is only known once the file loads, so the chain is rebuilt on each load.

### The whole audio chain is rebuilt each time

**Decision:** `applyAudioFilters()` builds the complete `af` string from the saved settings, and sets it at once.

**Why:** inferred. `af` is a single value, so rebuilding it from the settings keeps mpv and the saved settings in step, and avoids tracking each filter separately.

**Cost:** any filter added to `af` elsewhere would be removed. Keep all `af` changes in this function.

## Files and text

### Natural folder order with an explicit English collator

**Decision:** `mediaInFolder()` sorts with `QCollator(QLocale::English)` in numeric mode.

**Why:** a comment says the default collator "sorted 'ep10' before 'ep2'" on CI.

**Cost:** names sort in English order whatever the user's language. That is the trade for predictable results.

### File types from VLC 3.0, minus three

**Decision:** the extension lists are VLC 3.0's, without MIDI, SID and `.bin`.

**Why:** a comment says VLC's list is the goal, and those three are the ones "libmpv can't play".

**Cost:** these three extensions are not offered in the file dialog.

### Subtitles load like MPC-BE

**Decision:** `sub-auto` is `fuzzy`, and `sub-file-paths` lists the usual subtitle folders.

**Why:** a comment says "like MPC-BE".

**Cost:** fuzzy matching can load a subtitle that was not meant for the file. The user can load another with Ctrl+L.

## Settings and hotkeys

### Settings in QSettings

**Decision:** all settings use `QSettings` with the default INI file.

**Why:** inferred. It is Qt's standard store, needs no code, and is what the migration from `newmpc-be` relies on.

**Cost:** the defaults are repeated at each place a setting is read, so they can drift. See [Settings and data](Settings-and-Data.md#writing-a-new-setting).

### Hotkeys are named by their menu path

**Decision:** an action's id is `Menu/Item`, built from its menu and item text.

**Why:** the menu text is the one label every action already has, so no separate id table is needed. The Options Keys tab and the saved `keys/...` entries use the id.

**Cost:** renaming a menu item orphans its saved hotkey, silently, until the user sets it again. Change a menu label on purpose and note the change in the history.
