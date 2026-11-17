# Source map

Where things live, and which function to open first for each feature. Function names are stable; line numbers are left out because they go out of date.

## Files

| File | Lines | Contents |
|---|---|---|
| `main.cpp` | 61 | `main()`, `migrateOldName()`. Start-up and the single-window handshake. |
| `player.h` | 145 | The `Player` class: every method and member. Start here for the API. |
| `player.cpp` | 1,791 | The window and all playback logic. |
| `options.cpp` | 225 | `Player::openOptions()`, `Player::applySettings()`, `applyTheme()`, `hotkeyActions()`. |
| `mpris.h`, `mpris.cpp` | 110, 196 | `Mpris`, `MprisRoot`, `MprisPlayer`: the MPRIS2 interface and media keys. |
| `videowidget.h`, `videowidget.cpp` | 23, 65 | `VideoWidget`: draws mpv through the render API. |
| `tests/test_player.cpp` | 1,515 | `PlayerTest`: the GUI test suite. |
| `CMakeLists.txt` | 46 | Targets, Qt and libmpv lookup, install rules, CPack settings. |
| `.github/workflows/ci.yml` | 45 | The CI workflow. |
| `packaging/appimage/build-appimage.sh` | 34 | Manual AppImage build. |
| `data/reelplay.desktop` | 11 | Desktop entry: name, `Exec=reelplay %U`, MIME types for "Open With". |
| `data/reelplay.svg` | 5 | Application icon. |
| `data/copyright` | 19 | Debian copyright file, installed with the package. |
| `docs/screenshots/*.png` | | Screenshots used by the README. Generated from sample media. |
| `docs/wiki/` | | This wiki. |

## `player.cpp`, in order

| Section | What is there |
|---|---|
| Top | Includes. `JumpStyle` (left click jumps the seek bar), `ChapterSlider` (tick marks for chapters). |
| File-type tables | `mediaFilters`, `playlistFilters`, `openFilter`. The VLC 3.0 extension list, minus MIDI, SID and `.bin`. |
| Helpers | `mediaInFolder()` (natural sort), `readPlaylist()` (m3u, pls, xspf, asx, wpl, ram), `discImageKind()` (DVD and Blu-ray detection inside ISO images), `fmtTime()`, `parseTime()`. |
| `Player::Player()` | Builds the window: video area, controls, playlist dock, all menus, mpv options, media keys, property observers. |
| `shutdown()`, `closeEvent()`, `changeEvent()` | Ordered teardown, and re-grabbing media keys when the window is activated. |
| `command()`, `prop()`, `step()` | The mpv access helpers. |
| `openFiles()`, `playIndex()` | Turn arguments into playlist rows, then load one. |
| Transport | `play()`, `pause()`, `togglePause()`, `stop()`, `skipFile()`, `canSkip()`. |
| Track and chapter menus | `rebuildTrackMenus()`, `readChapters()`, `updateChapterMarks()`, `rebuildChapterMenu()`, `skipIntro()`. |
| Events | `handleEvents()`: the mpv event loop. `eventFilter()`: mouse and wheel on the video, hover on the seek bar. |
| Previews | `showPreview()`, `startPreviewJob()`, `showCachedPreview()`, `hidePreview()`. |
| Screenshots and recording | `autoScreenshotPath()`, `screenshot()`, `startScreenshotSeries()`, `startRecording()`, `stopRecording()`. |
| Sections | `saveAbSection()`. |
| Video | `setVideoAdjust()`, `openVideoAdjust()`, `rotateVideo()`, `setVideoFilter()`, `setCrop()`, `setScale()`, `setToneMap()`, `setAlwaysOnTop()`, `applyChrome()`, `toggleFullscreen()`, `zoom()`. |
| Stats and properties | `toggleStats()`, `updateStats()`, `propertiesText()`. |
| Audio | `applyAudioFilters()`, `setEqGains()`, `openEqualizer()`, `applyVisualizer()`, `setPassthrough()`, `rebuildAudioDeviceMenu()`. |
| Bookmarks and favourites | `addBookmark()`, `rebuildBookmarksMenu()`, `addFavorite()`, `rebuildFavoritesMenu()`. |
| After playback | `finishPlaylist()`. |
| Drag and drop | `dragEnterEvent()`, `dropEvent()`. |

## Feature to function

| Feature | Start at | Notes |
|---|---|---|
| Open a file, folder, disc, URL, CD | `openFiles()` | Expands playlists, detects DVD and Blu-ray folders and ISOs. |
| Play one row | `playIndex()` | Writes resume position for the old file, sets the disc device, loads the file. |
| Next, previous, folder walk | `skipFile()`, `canSkip()` | With one file open, steps through its folder (`folderNeighbors`). |
| What happens at end of file | `handleEvents()`, `MPV_EVENT_END_FILE` branch | See [Playback and files](Playback-and-Files.md#end-of-file). |
| Playlist editing | Playlist actions in `Player::Player()` (`plAct`) | Remove, randomise, save as M3U, clear. Drag reorder is the list widget's `InternalMove`. |
| Seek bar and previews | `eventFilter()`, `showPreview()` | Preview cache, ffmpeg job. |
| Chapters | `readChapters()`, `skipIntro()` | Title regex for intro and credits. |
| A-B repeat and section | `ab-loop-a`, `ab-loop-b` in the Play menu; `saveAbSection()` | Stream copy with ffmpeg. |
| Screenshots | `screenshot()`, `startScreenshotSeries()` | mpv's `screenshot-to-file`. |
| Stream recording | `startRecording()` | mpv's `stream-record`. |
| Bookmarks, favourites, resume | `addBookmark()`, `addFavorite()`; watch-later options in the constructor | Bookmarks and favourites are in settings; resume is mpv's watch-later files. |
| Colour, rotation, flip, deinterlace | `setVideoAdjust()`, `rotateVideo()`, `setVideoFilter()` | Settings saved as `video/...`. |
| Crop | `setCrop()` | Re-applied on `FILE_LOADED`. |
| Fullscreen and controls | `toggleFullscreen()`, `applyChrome()`, `idleTimer` | The dock is hidden in fullscreen and restored after. |
| Equaliser, normalise, pitch, compressor | `applyAudioFilters()` | Builds the whole `af` chain each time. |
| Visualiser | `applyVisualizer()` | Called on every `FILE_LOADED` and when the menu item toggles. |
| Passthrough | `setPassthrough()` | `audio-spdif`. Off by default. |
| Subtitles | Constructor (`sub-auto`, `sub-file-paths`); `Load Subtitle...` action | Track menus from `rebuildTrackMenus()`. |
| Options | `openOptions()`, `applySettings()` | Dialog writes settings; `applySettings()` re-applies all of them to mpv. |
| MPRIS | `Mpris::playerProperties()`, `MprisPlayer` methods | See [Desktop integration](Desktop-Integration.md#mpris). |
| Single window | `main()`, `openFromOtherInstance()` | See [Desktop integration](Desktop-Integration.md#single-window). |

## Other files

- **`options.cpp`**: `applySettings()` is the one place that pushes saved settings into mpv at start-up. `openOptions()` writes the Options dialog's keys, and calls `applySettings()` after OK. Other keys are written by the menus in `player.cpp`.
- **`mpris.cpp`**: `keyServices[]` lists the settings daemons that can send media keys. `Mpris::update()` is the diff-and-emit step.
- **`videowidget.cpp`**: `initializeGL()` creates the render context. `paintGL()` draws one frame into the widget's framebuffer, flipped vertically.
- **`tests/test_player.cpp`**: see [Testing](Testing.md).
