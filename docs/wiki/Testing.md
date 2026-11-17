# Testing

Reelplay's tests are GUI tests. They build the real `Player`, send it key and mouse events, and check libmpv's state through properties and the MPRIS interface. Nothing is mocked, so the tests need an X11 display, and some need a window manager and a session bus.

The suite is one QtTest binary, `test_player`, built from `tests/test_player.cpp` and the same sources as the app. It contains 73 test functions.

## Running

With a display already available:

```bash
./build/test_player                        # every test
./build/test_player playPauseHotkey        # one test
./build/test_player -v2 recordStream       # verbose output for one test
./build/test_player -functions             # list the test names
```

Through CTest, which CMake registers when Qt 6 Test is found:

```bash
cd build && ctest
```

Headless, the way CI runs it. It needs `xvfb`, `openbox`, `dbus-x11` and `libglib2.0-bin` installed:

```bash
xvfb-run -a dbus-run-session -- bash -c '
  openbox --sm-disable >/dev/null 2>&1 &
  sleep 3
  LIBGL_ALWAYS_SOFTWARE=1 REELPLAY_AO=null REELPLAY_VO=null ./build/test_player
'
```

- `xvfb-run` gives a virtual X11 display.
- `dbus-run-session` gives a private session bus, so the MPRIS and single-instance tests do not touch the desktop's own bus.
- `openbox` is the window manager. Focus and always-on-top tests need one.
- The `sleep 3` gives the window manager time to start before the tests run.

Running with a real desktop session works too, but the tests then share the desktop's session bus. The single-instance test skips if Reelplay is already running there.

## External tools

Some tests need programs outside the build. The CI installs all of them.

| Tool | Used by | When it is missing |
|---|---|---|
| `gdbus` (`libglib2.0-bin`) | MPRIS and single-instance tests | Those tests **fail**. They do not skip. |
| `python3` | `httpStream`, which serves a local file over HTTP | That test **fails**. It does not skip. |
| `xprop` (`x11-utils`) | `alwaysOnTop`, which checks the window manager's state | That test **skips**. |
| `ffmpeg` | Chapter titles, an audio-only test file, section cutting, seek previews, a file with cover art | Those tests **skip**. |

A skip is not a pass. QtTest prints each skip with its reason, and the totals show how many were skipped. Check the totals after a run.

The test media does not need ffmpeg. `makeClip()` and `encode()` create their files with libmpv's own encoder (`vo=lavc`, `ao=lavc`), from `av://lavfi:` sources. The shared 60-second clip is made once, in `initTestCase()`.

## Environment

| Variable | Effect |
|---|---|
| `REELPLAY_AO=null` | No audio device. Playback still runs, so the clock moves. |
| `REELPLAY_VO=null` | No video output. The crop and rotation checks in `videoTransforms` and `videoChoicesPersist` need a real output, so they only run when this is not `null` (the `realVideoOutput()` helper). |

## Isolation

- `initTestCase()` calls `QStandardPaths::setTestModeEnabled(true)`, so the settings and the watch-later folder go to test locations, not the user's.
- `init()` runs before each test. It clears `QSettings`, deletes the app-data folder, and creates a new `Player`.
- `cleanup()` deletes the `Player` after each test.

So each test starts with default settings and no resume files. A test that needs settings should write them itself, after `init()`.

## Helpers

These are members of `PlayerTest`:

| Helper | What it does |
|---|---|
| `newPlayer()` | Creates a `Player`, shows it, and waits until it is exposed and active. Window shortcuts only work in the active window. |
| `startClips()` | Opens the two test clips and waits until the status is Playing and the position is above zero. |
| `trigger(text)` | Triggers the first `QAction` whose text matches, including the `&` mnemonic (for example `"&Visualizer (audio files)"`). Fails the test if none matches. |
| `key(seq)` | Sends a key sequence to the player. |
| `status()`, `pos()`, `num(name)`, `p->prop(name)` | Read the status label, mpv's position, or an mpv property. |
| `sources()` | The `Qt::UserRole + 1` source of every playlist row. |
| `mouseMoveVideo(y)` | Sends a mouse move over the video at a height. |
| `onModal(fn)` | Runs `fn` on the next modal dialog. Needed because `exec()` blocks the test. |
| `gdbus`, `mpris`, `mprisGet` | Call the MPRIS interface from another process, as a desktop would. |
| `writeFile(name, bytes)` | Writes a file in the test's temporary folder. |
| `encode(path, src, opts)` | Encodes an `av://lavfi:` source with libmpv, with options given as `k=v|k=v`. Returns whether it worked. |
| `makeClip(path)` | Encodes the 60-second test clip with one video and one audio track. |

The `QTRY_*` macros are used for anything that depends on mpv having caught up, with a timeout of 5 or 10 seconds.

## Test groups

The 73 tests, grouped by what they cover:

| Group | Tests |
|---|---|
| **Transport and keys** (11) | `playPauseHotkey`, `stopHotkey`, `seekHotkeys`, `volumeAndMute`, `speedHotkeys`, `syncHotkeys`, `zoomHotkeys`, `goToTime`, `parseTimeFormats`, `abRepeat`, `repeatModes` |
| **Playlist and files** (21) | `playlistHotkeys`, `playlistEditing`, `recentFiles`, `openFolderNaturalOrder`, `discFolderDetected`, `formats`, `vlcPlaylistFormats`, `hlsManifestIsOneStream`, `discImagesAndIfo`, `fileUrlArguments`, `youtubeSupportOn`, `httpStream`, `nextPreviousWalkFolderOfLoneFile`, `autoNextInFolder`, `errorDoesNotFinishPlaylist`, `afterPlaybackExit`, `singleInstance`, `favorites`, `bookmarksPerFile`, `chapters`, `skipIntroChapter` |
| **Window and view** (15) | `fullscreenHotkeys`, `fullscreenAutoControls`, `singleClickPauses`, `doubleClickOnlyFullscreens`, `trackMenus`, `contextMenu`, `seekTooltip`, `chromeToggles`, `hiddenPartsSurviveFullscreen`, `alwaysOnTop`, `statsOverlay`, `propertiesDialogText`, `aspectRatio`, `resumePosition`, `settingsPersist` |
| **Video** (7) | `videoAdjustments`, `videoTransforms`, `videoFilters`, `videoChoicesPersist`, `autoScreenshot`, `screenshotSeries`, `seekPreviewFrames` |
| **Audio** (10) | `audioOutput`, `audioDevice`, `equalizerDialog`, `pitchShift`, `passthroughIsOptIn`, `visualizerForAudioOnly`, `visualiserDoesNotStallPlayback`, `coverArtForAudio`, `abSectionSave`, `recordStream` |
| **Settings and options** (3) | `optionsApplyAndPersist`, `subtitleOptions`, `subtitlesFromSubsFolder` |
| **Desktop** (6) | `mprisRegistered`, `mprisControls`, `mprisMetadata`, `mprisSignals`, `mediaKeysWhenFocused`, `settingsDaemonMediaKeys` |

`visualiserDoesNotStallPlayback` is the regression test for the stalled-playback bug. It fails without the fix in `applyVisualizer()`, and passes with it. See [mpv integration](mpv-Integration.md#track-selection-and-the-visualiser).

## Writing a test

A typical test:

1. Start with `startClips()`, or open the files it needs with `p->openFiles({...})`.
2. Do the thing: `key("Space")`, `trigger("&Menu item")`, or an MPRIS call.
3. Check the result with `QTRY_COMPARE(...)` or `QTRY_VERIFY(...)` on mpv's state, the status label, or the saved setting.

Guidelines from the existing tests:

- Check mpv's **state**, not the picture or the sound. CI has no audio or video output.
- Use `QTRY_*` for any value that changes after a command, because commands are asynchronous.
- Skip, do not fail, when an optional tool is missing: `if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) QSKIP("...");`.
- Name the test for the behaviour it protects, as `visualiserDoesNotStallPlayback` does, so a failure says what broke.
- Use `onModal()` for dialogs, and an `objectName` to find controls inside them.
- The class is a `friend` of `Player`, so a test can read any member. Prefer the public-facing check (a menu, a setting, mpv's state) when there is one.

## Known sensitive spots

- **Locale.** Folder sorting depends on the locale. The code uses an explicit English collator, because the default one sorted `ep10` before `ep2` on CI. Tests that sort names inherit that.
- **Asynchronous mpv.** `recordStream` reads `stream-record` back with `QTRY_COMPARE` because the set is asynchronous. Any new test that reads a property right after a command needs the same treatment.
- **Timeouts.** The tests wait up to 5 or 10 seconds for mpv. A very slow machine can time out. Raise the timeout rather than adding a sleep.
- **Focus.** Window shortcuts need an active window. `newPlayer()` waits for activation, and CI waits three seconds for openbox first.
- **Null outputs.** With `REELPLAY_VO=null`, some crop and rotation checks are not run. A change to those features needs a run with a real video output.
- **Desktop bus.** Run the desktop-bus tests under `dbus-run-session`, as in CI, or they will talk to the real desktop.
