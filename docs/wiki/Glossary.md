# Glossary

Terms used across the code and the docs.

| Term | Meaning |
|---|---|
| **A-B repeat** | Looping a section between two markers, A and B. mpv's `ab-loop-a` and `ab-loop-b`. |
| **ab-loop** | mpv's properties for the A-B markers. |
| **af** | mpv's audio filter chain, a single string. Reelplay builds the whole chain each time. |
| **aid, vid, sid** | mpv's track ids for audio, video and subtitles. `no` means none. |
| **AC3, E-AC3, DTS, DTS-HD, TrueHD** | Surround sound formats that a receiver can decode. Passthrough sends them undecoded. |
| **Always on Top** | A window-manager state that keeps a window above others. Set on the `QWindow`. |
| **Argument (of the app)** | A file, folder, URL or playlist given on the command line, or sent by a second launch. |
| **AppData** | `~/.local/share/reelplay`, where Qt puts per-app data. Holds the watch-later folder. |
| **atempo, asetrate, aresample** | ffmpeg audio filters. Reelplay uses them together for pitch shifting. |
| **Bookmark** | A saved time in one file, listed for that file only. |
| **BDMV** | The folder inside a Blu-ray disc. Its presence means a Blu-ray. |
| **Chapter** | A named part of a file, with a start time. mpv's `chapter-list`. |
| **checkable** | A menu item that is ticked or not. Reelplay's `checkable()` helper. These are not remappable. |
| **Cinnamon, GNOME, MATE** | Desktops whose settings daemons can send media keys to Reelplay. |
| **Crop** | Trimming the picture to a given aspect ratio, centred. |
| **D-Bus** | The message bus for desktop services. Reelplay uses the session bus for MPRIS and single-window, and the system bus for logind. |
| **Deinterlace** | Converting interlaced video to progressive frames. |
| **Denoise, Sharpen** | Video filters: `hqdn3d` and `unsharp`. |
| **Equaliser** | Ten gain bands from 31 Hz to 16 kHz, with a compressor and stereo widen. |
| **Favourite** | A saved time in one file. One per file, listed in the Favorites menu. |
| **Fusion** | Qt's cross-platform widget style. Used by the dark theme. |
| **GrabMediaPlayerKeys** | The settings-daemon method that makes an app the target of media keys. |
| **handleEvents** | Reelplay's loop over mpv's event queue, in `player.cpp`. |
| **hwdec** | mpv's hardware decoding mode. Off by default in Reelplay. |
| **IFO, VIDEO_TS** | DVD structure files and folder. `VIDEO_TS.IFO` opens the DVD. |
| **ISO** | A disc image. Reelplay checks inside it for DVD or Blu-ray structure. |
| **libmpv** | mpv as a library. The playback engine. |
| **lavfi, lavfi-complex** | mpv's bridge to ffmpeg filter graphs. `lavfi-complex` replaces the whole video and audio graph. |
| **Loop file** | mpv's `loop-file`. Repeat File uses `inf`. |
| **MPRIS** | Media Player Remote Interfacing Specification. A D-Bus standard for media players. |
| **Natural order** | Sorting so that `ep2` comes before `ep10`. |
| **newmpc-be** | The project's previous name, up to version 0.3.1. |
| **OSD** | On-screen display: text drawn over the video by mpv (`show-text`). |
| **Passthrough** | Sending compressed audio to a receiver without decoding it. Also called S/PDIF. |
| **Playlist dock** | The side panel that holds the playlist. Shown with Ctrl+7. |
| **QCollator** | Qt's locale-aware string comparison. Used for natural folder order. |
| **QSettings** | Qt's store for settings. Reelplay's are in `~/.config/reelplay/reelplay.conf`. |
| **QTRY_* macros** | QtTest macros that retry a check until it passes or times out. Used for mpv state that changes after a command. |
| **Render API** | mpv's way of drawing into an app's own OpenGL context. Used on Wayland. |
| **Resume** | Starting a file where it was left. mpv's watch-later files. |
| **Seek bar** | The slider above the buttons. Shows position and chapters, and previews frames. |
| **Single window** | Second launches hand their files to the first window, and exit. |
| **Stream copy** | Copying packets without decoding or encoding them. `-c copy` in ffmpeg. |
| **Stream recording** | Saving a stream as it plays. mpv's `stream-record`. |
| **Track** | A video, audio or subtitle stream in a file. mpv's `track-list`. |
| **vf** | mpv's video filter chain. Reelplay labels its filters, so they can be added and removed. |
| **Visualiser** | A spectrum drawn in place of the picture for an audio file. |
| **VLC 3.0** | The player whose file-type list Reelplay uses for its open dialog. |
| **Wakeup callback** | mpv's call that tells a client an event is waiting. It runs on mpv's thread. |
| **wid** | mpv's option for drawing into an existing native window, by window id. Used on X11. |
| **Watch-later** | mpv's per-file resume store. Reelplay keeps its files in `watch_later/`. |
| **Wayland, X11** | The two Linux display systems. X11 embeds the video; Wayland uses the render API. |
