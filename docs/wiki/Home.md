# Reelplay wiki

Reelplay is a classic-style media player for Linux. It is written in C++17, uses Qt 6 for the window and libmpv for playback, and keeps the look and keyboard feel of MPC-BE. It is not affiliated with that project.

This wiki is for people who build, change, test or package Reelplay. For end-user features, keyboard shortcuts and install instructions, read the [README](../../README.md).

## Pages

| Page | What it covers |
|---|---|
| [Building and running](Building-and-Running.md) | Dependencies, build, run, packaging, environment variables |
| [Architecture](Architecture.md) | The parts, threads, startup and shutdown order |
| [Source map](Source-Map.md) | What each file holds and where each feature lives |
| [mpv integration](mpv-Integration.md) | How Reelplay drives libmpv: options, properties, events, filter graphs |
| [Playback and files](Playback-and-Files.md) | Opening files, folders, discs and playlists; the playlist model; end-of-file rules |
| [Playback features](Playback-Features.md) | Seek bar, previews, A-B repeat, section saving, screenshots, recording, chapters, resume |
| [Video features](Video-Features.md) | Colour adjustments, rotation, crop, scaling, filters, window and fullscreen |
| [Audio features](Audio-Features.md) | Devices, normalising, equaliser, pitch, passthrough, visualiser, subtitles |
| [UI and menus](UI-and-Menus.md) | Window layout, menus, hotkeys, the Options dialog |
| [Desktop integration](Desktop-Integration.md) | Single window over D-Bus, MPRIS, media keys, power actions, desktop entry |
| [Settings and data](Settings-and-Data.md) | Every saved setting, where data lives, the newmpc-be migration |
| [Testing](Testing.md) | The test harness, what each group covers, running tests headless |
| [CI and release](CI-and-Release.md) | The GitHub Actions workflow, packaging, and how a release is made |
| [Contributing](Contributing.md) | Conventions and checklists for common changes |
| [Troubleshooting](Troubleshooting.md) | Symptoms, causes and fixes |
| [Design decisions](Design-Decisions.md) | Why the code is shaped the way it is |
| [Glossary](Glossary.md) | Terms used across the code and the docs |
| [History](History.md) | Versions and what changed in each |

## At a glance

- **Version:** 0.8.0 (`CMakeLists.txt`). **Licence:** GPL-3.0-or-later.
- **Size:** about 2,600 lines of application code in 8 files, plus a 1,500-line GUI test file with 76 test functions. Most of the logic is in `player.cpp`.
- **Platforms:** X11 is the tested path, on Linux Mint 22.3 and in CI on Ubuntu 24.04 (Qt 6.4, mpv 0.37). Wayland has a code path, but it has not been tested in a real Wayland session.
- **Division of labour:** Reelplay owns the window, menus, playlist, settings and desktop integration. libmpv does the demuxing, decoding, rendering and track selection.

## Where to start reading the code

1. [`main.cpp`](../../main.cpp): startup and the single-window handshake (61 lines).
2. [`player.h`](../../player.h): the `Player` class. Every method and member the window uses is declared there.
3. `Player::Player()` in [`player.cpp`](../../player.cpp): builds the UI and sets the mpv options.
4. `Player::handleEvents()` in `player.cpp`: where mpv events become UI updates and playlist advances.

## Known gaps

- **Wayland** is not tested in a real session. The README says so too.
- **CI has no sound or picture checks.** It runs with null audio and video outputs, so nothing in CI can tell whether audio or video is right. The tests check mpv's state (properties, track lists, positions) instead.
- **Stream recording** is marked experimental by mpv. See [Playback features](Playback-Features.md#stream-recording-experimental).
- **Windows and macOS** are not supported. The code is Linux-only (D-Bus, X11 embedding, the Debian package).
