# Building and running

## Requirements

| Need | Used for | Ubuntu 24.04 package |
|---|---|---|
| CMake 3.16 or newer | Build system | `cmake` |
| A C++17 compiler | Build | `build-essential` |
| pkg-config | Finding libmpv | `pkg-config` |
| Qt 6 Widgets, OpenGLWidgets, DBus | The app | `qt6-base-dev` |
| Qt 6 Test | `test_player` (optional; the build skips tests without it) | part of `qt6-base-dev` |
| libmpv, with headers | Playback engine (tested with 0.37) | `libmpv-dev` |
| OpenGL headers | Video widget | `libgl-dev` |

Runtime tools, all optional:

| Tool | Needed for |
|---|---|
| `ffmpeg` | Seek-bar frame previews (Playback > Seek bar hover), Play > A-B Repeat > Save Section |
| `yt-dlp` | Sites such as YouTube. mpv calls it when `ytdl` is on, which it always is in Reelplay |

Screenshot series does **not** use ffmpeg. It calls mpv's own `screenshot-to-file`. (The README currently lists ffmpeg for it; that line is out of date.)

Install everything for a build on Ubuntu 24.04 or Linux Mint 22:

```bash
sudo apt install cmake pkg-config qt6-base-dev libgl-dev libmpv-dev ffmpeg
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/reelplay [files...]
```

The build produces two executables from the same sources:

- `build/reelplay`: the player.
- `build/test_player`: the GUI test binary, only if Qt 6 Test is found. See [Testing](Testing.md).

The source list is shared, so `reelplay` and `test_player` always compile the same code.

## Run

Pass files, folders, URLs or playlists as arguments. They open in the window in the order given, and the first one plays.

```bash
./build/reelplay ~/Videos/film.mkv
./build/reelplay "https://example.com/stream.m3u8"
```

Existing relative file paths are turned into absolute paths before they are sent to a running instance, so `reelplay b.mkv` works from any directory when a window is already open. See [Desktop integration](Desktop-Integration.md#single-window).

## Package

```bash
cmake --build build --target package
```

This writes `build/reelplay_<version>_amd64.deb`, where the version comes from `project(reelplay VERSION ...)` in `CMakeLists.txt`. Install it with:

```bash
sudo apt install ./build/reelplay_0.8.0_amd64.deb
```

The package:

- installs `/usr/bin/reelplay`, the desktop entry, the SVG icon and the copyright file;
- depends on the libraries the binary links to (`CPACK_DEBIAN_PACKAGE_SHLIBDEPS`) and recommends `qt6-wayland`;
- replaces and conflicts with `newmpc-be`, the old name of the project, so an upgrade replaces it cleanly.

## AppImage

`packaging/appimage/build-appimage.sh` builds an AppImage with linuxdeploy and its Qt plugin. It needs `qmake6` (`qt6-base-dev-tools`) and `libmpv-dev` on the build machine.

The script downloads linuxdeploy from GitHub's "continuous" releases **without checksums**, so it is not run by CI and not part of a release. Run it only if you accept those downloads.

## Environment variables

These are read by the app or the tests. None is needed for normal use.

| Variable | Read in | Effect |
|---|---|---|
| `REELPLAY_RENDER=gl` | `Player::Player()` | Draw video through the libmpv render API (`VideoWidget`) even on X11. Used to exercise the Wayland path. |
| `REELPLAY_AO=<name>` | `Player::Player()` | Sets mpv's `ao`. `null` means no audio device. CI sets it. |
| `REELPLAY_VO=<name>` | `Player::Player()` | Sets mpv's `vo`. `null` means no video output. CI sets it. |
| `LIBGL_ALWAYS_SOFTWARE=1` | Mesa (not Reelplay) | Software OpenGL for CI runners without a GPU. |

## Notes for building and running

- `main()` sets `LC_NUMERIC` to `C`, because libmpv needs the C numeric locale. A locale with a decimal comma would otherwise break mpv's number parsing.
- At start-up, `migrateOldName()` moves settings and data from the old `newmpc-be` folders if the new ones do not exist yet. See [Settings and data](Settings-and-Data.md#migration-from-newmpc-be).
- Without `-DCMAKE_BUILD_TYPE=Release`, CMake builds with no optimisation. Use `Release` for everyday use.
