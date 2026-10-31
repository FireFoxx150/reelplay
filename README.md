# Reelplay

Classic-style media player for Linux on Qt 6 + libmpv, inspired by MPC-BE.
(Formerly newmpc-be; not affiliated with the MPC-BE project.)

## Build

```bash
sudo apt install cmake qt6-base-dev libmpv-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/reelplay [files...]
```

## Install (.deb)

```bash
cmake --build build --target package
sudo apt install ./build/reelplay_0.5.0_amd64.deb
```

Adds a menu entry and an icon, and offers reelplay under "Open With" for video and audio
files. To make it the default player: right-click a video, Properties, then Open With.

## Single window

Opening a file while reelplay is running (e.g. double-clicking in the file manager)
plays it in the existing window instead of starting a second one, like MPC-BE. Turn it
off in Options > Player. Also there: "When a file ends, play the next file in its
folder" (off by default).

## Picture, sound and favorites

- **View > Video:** adjust brightness, contrast, saturation, gamma and hue; rotate;
  flip horizontally; deinterlace; crop to 4:3, 16:9, 1.85:1 or 2.35:1 (centred).
- **Play > Audio Output:** pick the output device, normalize volume, and equalizer presets
  (Flat, Bass Boost, Treble Boost, Voice).
- **Play > After Playback:** do nothing, exit, shut down or suspend when the playlist ends.
- **Favorites:** remember a file at its current position and jump back to it later.

## Subtitles

Subtitles named like the video (e.g. `film.en.srt` for `film.mkv`) load automatically,
also from a `Subs/` or `Subtitles/` subfolder. Font, size, color and position are in
Options > Subtitles.

## Formats

Plays what VLC plays, through libmpv/FFmpeg: all of VLC 3.0's video and audio file
extensions (Open Folder picks them up), its file types for "Open With", and its playlist
formats (.m3u/.m3u8, .pls, .xspf, .asx/.wax/.wvx, .wpl, .ram, .cue). Also DVD/Blu-ray
folders, `VIDEO_TS.IFO` and DVD/Blu-ray `.iso` images, Audio CD (File > Open Audio CD),
and streams (http, rtsp, rtmp, srt, udp, mms, HLS). `./build/test_player formats` plays a
generated file for 42 container/codec combinations.

Not supported: MIDI and SID music (VLC needs extra plugins for those too).
Sites like YouTube go through yt-dlp, which must be recent: Mint's packaged version
(2024.04) no longer works with YouTube.

## Multimedia keys

Play/Pause, Stop, Next and Previous keys control the player even when its window isn't
focused. On Cinnamon, GNOME and MATE the player claims the keys from the desktop
(GrabMediaPlayerKeys), so it gets them even if a browser is also open; whichever player
you used last wins. It's also an MPRIS player, so it shows up in the sound applet and
works with `playerctl` and other desktops' media keys. Volume keys stay with the system
volume, as in VLC.

With a single file open (e.g. double-clicked in the file manager), Next/Previous (media
keys, PgDn/PgUp, the toolbar buttons) move to the next/previous file in its folder, like
MPC-BE.

## X11 and Wayland

On X11 mpv draws straight into the window (fastest startup). On Wayland it draws through
libmpv's OpenGL render API, because Wayland can't embed another program's window. To try
the Wayland code path on X11: `REELPLAY_RENDER=gl ./build/reelplay`.

## Hotkeys (MPC-BE defaults)

| Key | Action |
|---|---|
| Space | Play/Pause |
| . | Stop |
| Left / Right | Jump 5 s |
| Shift+Left / Right | Jump 30 s |
| Ctrl+Left / Right | Frame step back / forward |
| PgUp / PgDn | Previous / next file |
| Up / Down, mouse wheel | Volume |
| Ctrl+M | Mute |
| Alt+Enter, double-click | Fullscreen (Esc exits) |
| Ctrl+7 | Playlist |
| Ctrl+Up / Ctrl+Down / Ctrl+R | Speed +0.1 / -0.1 / reset |
| F1 / F2 | Subtitle delay -/+ 100 ms |
| Numpad - / + | Audio delay -/+ 10 ms |
| [ / ] / \\ | Set A-B repeat marker A / B / clear |
| Alt+1 / Alt+2 / Alt+3 | Zoom 50% / 100% / 200% |
| Right-click | Full menu |
| F5 / Alt+I | Save screenshot (auto-named / choose file) |
| Shift+F10 | File properties |
| O | Options (hardware decoding, languages, screenshot folder, dark theme, hotkeys) |
| Delete (in playlist) | Remove selected |
| Ctrl+O / Ctrl+U / Ctrl+L | Open file / URL / subtitle |
| Ctrl+D | Open DVD/BD (folder with VIDEO_TS or BDMV) |
| Ctrl+G | Go to time (hh:mm:ss, mm:ss or seconds) |
| Ctrl+PgDn / Ctrl+PgUp | Next / previous chapter (also Play > Chapters) |
| Ctrl+A | Always on top |
| Ctrl+0 / Ctrl+1 / Ctrl+2 | Hide menu bar / controls / status bar |
| Ctrl+J | Statistics overlay (codec, size, dropped frames, sync) |
| Ctrl+Shift+F | Add the current file to Favorites |
| Ctrl+C | Close file |
| Alt+X | Exit |

Also: aspect ratio presets (View), repeat file/playlist (Play), Recent Files and
Open Folder (File; natural sort, e.g. ep2 before ep10), seek-bar hover time.
Playlist: drag to reorder, right-click for Remove / Randomize / Save (.m3u) / Clear;
.m3u/.m3u8 files open as playlists. In fullscreen, move the mouse to the bottom edge
for the controls; the cursor hides after 2 s.

Volume, window size/position, playlist panel and repeat mode are remembered between runs
(`~/.config/reelplay/`). Each file resumes where you left off
(`~/.local/share/reelplay/watch_later/`).

## Tests

GUI tests drive the real window with key and mouse events (needs an X11 display):

```bash
./build/test_player
```

## Licence

GPL-3.0-or-later (see `LICENSE`). Copyright 2026 Matthew Allwood. Reelplay links to
libmpv and FFmpeg, which Debian/Ubuntu build as GPL-3, so it must stay GPL-compatible.
