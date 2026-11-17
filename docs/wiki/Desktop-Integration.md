# Desktop integration

How Reelplay fits into a Linux desktop: one window for all files, MPRIS for the sound applet and `playerctl`, media keys, power actions after playback, and the desktop entry. The code is in `main.cpp` (single window), `mpris.cpp` and `mpris.h` (MPRIS and media keys), and `player.cpp` (power actions).

## Single window

The **Use a single window** option (Options > Player, on by default) makes a second launch hand its files to the first window and exit. It is read once, at launch.

How it works, in `main()`:

1. Each argument that is an existing file is turned into an absolute path. URLs are left alone. This is what lets `reelplay b.mkv` work from any directory.
2. If the setting is on and the session bus is connected, try to register the bus name `org.reelplay.Instance`. `registerService()` is atomic, so two launches starting at the same moment cannot both win.
3. If the name is already taken, call `openFromOtherInstance(files)` on `/org/reelplay` (interface `org.reelplay.Player`), with a five-second timeout.
   - If it answers, this process exits with status 0.
   - If it does not answer within five seconds, this process opens its own window. A busy first instance that takes longer than five seconds to answer therefore causes a second window.
4. If this process won the name, it exports the window at `/org/reelplay` once the window exists, so later launches can reach it.

`openFromOtherInstance()` opens the files, which replaces the playlist. It then restores the window if it is minimised, raises it, and activates it.

Without a session bus, the setting has no effect and every launch opens a window.

Test: `singleInstance`. It skips when another Reelplay already owns the name on the desktop, because the test needs the name for itself.

## MPRIS

MPRIS2 is the standard that desktop sound applets, `playerctl` and media-key daemons use to control media players. Reelplay exports two interfaces from `Mpris`.

**Service:** `org.mpris.MediaPlayer2.reelplay`. If that name is taken (for example by another Reelplay window), the name gets `.instance<pid>` appended. Object path: `/org/mpris/MediaPlayer2`.

### Root interface (`org.mpris.MediaPlayer2`)

| Member | Value or behaviour |
|---|---|
| `CanQuit`, `CanRaise` | `true` |
| `HasTrackList` | `false` |
| `Identity` | `Reelplay` |
| `DesktopEntry` | `reelplay` (so the desktop uses the matching `.desktop` file) |
| `SupportedUriSchemes` | file, http, https, rtsp, rtmp, mms, mmsh, srt, udp, ftp, smb, dvd, bd, cdda |
| `SupportedMimeTypes` | Common video, audio and playlist types, from matroska and MP4 to FLAC, Ogg and M3U |
| `Raise()` | Shows, raises and activates the window. |
| `Quit()` | Closes the window. |

### Player interface (`org.mpris.MediaPlayer2.Player`)

| Property | Value |
|---|---|
| `PlaybackStatus` | `Stopped`, `Paused` or `Playing`. |
| `Metadata` | `mpris:trackid` (`/org/reelplay/track/<row>`, or `/NoTrack` when nothing is loaded); `xesam:url`; `xesam:title` (mpv's `media-title`, else the row's text); `mpris:length` in microseconds; `xesam:artist` and `xesam:album` when the file has them. |
| `Volume` | 0 to 1. Mapped to mpv's 0 to 100. Writable. |
| `Position` | Current position in microseconds; 0 when stopped. |
| `Rate` | mpv's `speed`; 1.0 when stopped. Writable from 0.25 to 4. Zero and negative values are ignored, because the specification uses zero to mean pause. |
| `MinimumRate`, `MaximumRate` | 0.25 and 4. |
| `CanGoNext`, `CanGoPrevious` | From `canSkip()`. |
| `CanPlay`, `CanPause` | True when the playlist is not empty. |
| `CanSeek` | True when something is loaded and its duration is known. |
| `CanControl` | `true` |

| Method | Behaviour |
|---|---|
| `Play`, `Pause`, `PlayPause`, `Stop` | As the buttons and keys. |
| `Next`, `Previous` | `skipFile(±1)`. |
| `Seek(offset)` | Relative seek, in microseconds. Ignored when stopped. |
| `SetPosition(track, position)` | Absolute seek. Ignored if the track id is not the current one, as the specification asks. |
| `OpenUri(uri)` | Replaces the playlist with the URI. |

| Signal | When |
|---|---|
| `Seeked(position)` | On mpv's `PLAYBACK_RESTART`, which follows every seek. |
| `PropertiesChanged` (from `org.freedesktop.DBus.Properties`) | After each event-loop pass, for each property whose value changed since the last signal. |

`Mpris::update()` does the comparing. It keeps the last values in `last` and sends only the changes.

### Using it

```bash
playerctl -p reelplay status
playerctl -p reelplay play-pause
playerctl -p reelplay position 30        # seek to 30 seconds
gdbus call --session --dest org.mpris.MediaPlayer2.reelplay \
    --object-path /org/mpris/MediaPlayer2 \
    --method org.mpris.MediaPlayer2.Player.PlayPause
```

Tests: `mprisRegistered`, `mprisControls`, `mprisMetadata`, `mprisSignals`. They call the interface through `gdbus`, the same way a desktop would.

## Media keys

Two routes get the keys to Reelplay.

**1. The desktop routes media keys to MPRIS.** Desktops that use MPRIS for media keys send them to one registered player, which is not always the one the user wants (see below).

**2. Reelplay grabs them from the settings daemon.** Cinnamon, GNOME and MATE only fall back to MPRIS in a fixed order (the first player that ever registered, which may be a browser), so Reelplay also asks the daemon for the keys:

| Daemon | Path | Interface |
|---|---|---|
| `org.gnome.SettingsDaemon` | `/org/gnome/SettingsDaemon/MediaKeys` | `org.gnome.SettingsDaemon.MediaKeys` (Cinnamon also uses this name) |
| `org.gnome.SettingsDaemon.MediaKeys` | the same | the same |
| `org.mate.SettingsDaemon` | `/org/mate/SettingsDaemon/MediaKeys` | `org.mate.SettingsDaemon.MediaKeys` |

`grabMediaKeys()` sends `GrabMediaPlayerKeys("reelplay", 0)` to each one. Time `0` means "now", which makes Reelplay the newest grabber. It runs at start-up, and again whenever the window becomes active (`changeEvent()`), so **the player the user used last gets the keys**. Missing daemons just return an error, which is ignored. `ReleaseMediaPlayerKeys` runs in the destructor.

Key handling: the daemon sends `MediaPlayerKeyPressed(application, key)` to everyone. `Mpris` only acts when `application` is `reelplay`.

| Key | Action |
|---|---|
| Play | Toggle pause |
| Pause | Pause |
| Stop | Stop |
| Next, Previous | Next and previous file |
| FastForward, Rewind | Seek +10 s and −10 s |

**With the window focused**, Qt handles the same keys as shortcuts (`Key_MediaTogglePlayPause`, `Key_MediaPlay`, `Key_MediaPause`, `Key_MediaStop`, `Key_MediaNext`, `Key_MediaPrevious`). In that case the Play key calls `play()`, not a toggle. The difference is small, but it is in the code.

**Volume keys are not taken.** They stay with the system volume, as in VLC.

Tests: `mediaKeysWhenFocused`, `settingsDaemonMediaKeys`.

## Power actions

Play > After Playback chooses what happens when the playlist ends at end of file (not when the user presses Stop, and not after a file fails to load). See [Playback and files](Playback-and-Files.md#after-playback).

- **Exit Player** closes the window.
- **Shut Down** calls `PowerOff(true)` on `org.freedesktop.login1.Manager` (logind), on the **system** bus.
- **Suspend** calls `Suspend(true)` on the same interface.

The `true` argument means "interactive": polkit may ask for a password, so the request can wait for the user. The call is made with `QDBus::NoBlock`, because logind acknowledges it and nothing else needs to wait. The app does not check whether logind accepted it.

The Exit option is tested (`afterPlaybackExit`). Shut Down and Suspend are not, because a test cannot power off the machine.

## Desktop entry and icon

`data/reelplay.desktop` is installed to `/usr/share/applications`:

- `Exec=reelplay %U`, so files passed by a file manager arrive as `file://` URLs, which `openFiles()` converts;
- `Icon=reelplay`, and `data/reelplay.svg` is installed to the hicolor theme;
- `MimeType` lists audio, video, playlist and disc types, which puts Reelplay in "Open With" for them;
- `Categories=AudioVideo;Video;Audio;Player;Qt;`

The app also calls `setDesktopFileName("reelplay")`, so Wayland matches the window to this file, and `QIcon::fromTheme("reelplay")` for the window icon.
