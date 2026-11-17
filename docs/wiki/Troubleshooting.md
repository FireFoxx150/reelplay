# Troubleshooting

Symptoms a user or a developer may hit, with the cause and what to do. Causes marked "by design" are behaviour the code intends.

## Playback

### Sound stops after turning the visualiser off

**Cause:** mpv drops the audio track while the spectrum graph is running and does not give it back when the graph is removed. The clock stops and the playlist does not advance.

**Fix:** update to commit `aac1739` or later. The regression test is `visualiserDoesNotStallPlayback`. If it still happens, check `aid` and `vid` after toggling. See [mpv integration](mpv-Integration.md#track-selection-and-the-visualiser).

### Picture stays black after the visualiser

**Cause:** the same mpv behaviour, for the video track.

**Fix:** as above.

### Attached cover art stops the visualiser from running

**Cause:** an MP3 with cover art has a video track (the picture), so the visualiser does not apply.

**By design.** The file's picture is shown instead of the spectrum.

### "Cannot play: ..." in the status bar

**Cause:** mpv could not open the file. The message is mpv's own error.

**Fix:** check the file and the path. If there is a next file in the playlist, it plays.

### After Playback did not run after a file failed

**Cause:** the action runs only when the playlist ends at the end of a file. A file that fails to load is not treated as finished.

**By design.**

### Playback is slow or the CPU is busy

**Cause:** hardware decoding is off by default (`hwdec` is `no`).

**Fix:** choose a mode in Options > Player > Hardware decoding. It applies when you click OK. Which mode works depends on your GPU and driver.

### Resume position was not saved after a crash

**Cause:** the resume file is written when you switch files, choose Close, and on a clean exit while something is loaded. A crash or a kill writes nothing.

**By design.** The resume file from the last of those points is used.

### Save Section starts a little before A

**Cause:** the section is cut with stream copy (`-c copy`), so it can only start on a keyframe, at or before A.

**By design.** Exact cuts would need re-encoding, which Reelplay does not do.

### Stream recording is missing its start, or will not play

**Cause:** recording starts when you choose it, so a stream that is already part way through can begin with frames that cannot be decoded. The file is written when recording stops. Stream recording is marked experimental, by mpv and by Reelplay.

**Fix:** start the recording before the part you want, and stop it to finish the file.

### Seek-bar previews do not appear

**Cause:** previews are only made for local files, and they need `ffmpeg`.

**Fix:** install `ffmpeg`, and use a local file. Streams show the time only.

### YouTube or another web page does not play

**Cause:** mpv calls `yt-dlp` for these, and `yt-dlp` is missing or out of date. Linux Mint's packaged version (2024.04) no longer works with YouTube, as the README says.

**Fix:** install a recent `yt-dlp` and make sure it is on `PATH`.

### Sound is silent or garbled with S/PDIF passthrough

**Cause:** passthrough sends AC3, E-AC3, DTS and TrueHD undecoded. A stereo output cannot play them.

**Fix:** turn passthrough off (Play > Audio Output > S/PDIF Passthrough). Turn it on only with a receiver on the selected output. It is off by default.

## Window and desktop

### A second launch opens a new window

Possible causes, in order of likelihood:

1. **Single window is off.** Check Options > Player > Use a single window. The setting is read at launch, so restart after changing it.
2. **No session bus.** Check that `DBUS_SESSION_BUS_ADDRESS` is set in the environment that launches Reelplay.
3. **The first window did not answer within five seconds.** A busy first window can take longer than that.

To see whether the name is registered:

```bash
busctl --user list | grep reelplay
```

### Media keys control another player (Cinnamon, GNOME or MATE)

**Cause:** these desktops send media keys to the MPRIS player that was registered first, or the most recently activated grabber. Reelplay grabs the keys when its window becomes active.

**Fix:** click the Reelplay window once. The player that was activated last gets the keys.

### Volume keys do not change Reelplay's volume

**By design.** Volume keys stay with the system volume, as in VLC. Use the mouse wheel over the video, Up and Down, or the volume slider.

### Playlist order is wrong

**Cause:** folders are sorted naturally (`ep2` before `ep10`). Playlists and files given on the command line keep the order they were given in.

**Fix:** check the file names, and for a playlist, its order.

### Switching the dark theme off keeps the Fusion style

**Cause:** `applyTheme(false)` restores the original palette, but not the Fusion style, which stays until the next launch. This comes from reading the code. It has not been reproduced in a session.

**Fix:** restart Reelplay. A fix would also reset the style when the theme is turned off.

### Starts slowly on X11

**Cause:** the render path costs 0.5 to 1 s of start-up on the author's NVIDIA machine. On X11 the default is to embed mpv in a window, which is fast. The render path is used on Wayland, or when `REELPLAY_RENDER=gl` is set.

**Fix:** check whether `REELPLAY_RENDER` is set in your environment.

### Wayland

The Wayland path (the render API) is written but **not tested in a real Wayland session**. If it misbehaves, run the app from a terminal and note the output, and say whether `XDG_SESSION_TYPE` is `wayland`.

## Keys and settings

### A shortcut I want to change is not in Options > Keys

**Cause:** not every shortcut is remappable. Checkable items (Always on Top, the three Hide items, Statistics, Mute and the video filters), Ctrl+B, Ctrl+Shift+F, the media keys, Record Stream, and the playlist's keys cannot be changed in the Options dialog.

**Fix:** none in the app yet. See [UI and menus](UI-and-Menus.md#hotkeys). The README says every shortcut can be changed, which is not true.

### Screenshot series says it needs ffmpeg

**Cause:** the README says so, and that line is out of date. Screenshot series uses mpv's own `screenshot-to-file`, so it does not need ffmpeg.

## Build and tests

### A test is skipped

**Cause:** an optional tool is missing: `ffmpeg` for several tests, `xprop` for `alwaysOnTop`. The skip message says which.

**Fix:** install the tool, or accept the skip. See [Testing](Testing.md#external-tools).

### A test fails only in CI

**Cause:** usually the locale, a timeout on a slow runner, or the window manager not being ready.

**Fix:** reproduce with the same environment variables, and check the locale. See [CI and release](CI-and-Release.md#reproducing-ci-locally).

### The build fails with a missing `mpv` package

**Cause:** pkg-config cannot find libmpv.

**Fix:** install `libmpv-dev`. Check with `pkg-config --modversion mpv`.
