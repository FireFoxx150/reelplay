# Playback and files

How Reelplay turns what the user opens into playlist rows, how a row is played, and what happens when a file ends. The entry points are `openFiles()`, `playIndex()`, `skipFile()` and the `END_FILE` branch of `handleEvents()`.

## What the user can open

Each kind of input goes through a different branch of `openFiles()`:

| Input | Handled as | Result in the playlist |
|---|---|---|
| A media file | Taken as is | One row. mpv decides whether it can play it. |
| A playlist (`.m3u`, `.m3u8`, `.pls`, `.xspf`, `.asx`, `.wax`, `.wvx`, `.wpl`, `.ram`) | Read by `readPlaylist()`. Each entry is pushed back onto the queue. | One row per entry. |
| An HLS manifest (`#EXT-X-` in an `.m3u8`) | Not expanded. Passed to mpv as one item. | One row, one stream. |
| `.cue` and `.sdp` | Not parsed by Reelplay. Passed to mpv as one item. | One row. |
| `VIDEO_TS.IFO` | Replaced by its folder. | One DVD row. |
| A folder with `VIDEO_TS`, or named `VIDEO_TS` | `dvd://` with the folder as the source. | One DVD row. |
| A folder with `BDMV` | `bd://` with the folder as the source. | One Blu-ray row. |
| Any other folder | Every media file directly inside (not recursive), in natural order. | One row per file. |
| An `.iso` | `discImageKind()` reads up to 32 MiB and looks for `VIDEO_TS` (DVD) or `BDMV` (Blu-ray). | One DVD or Blu-ray row. A normal ISO is treated as a file. |
| A string containing `://` | Not checked, passed to mpv. | One row. Covers http, https, rtsp, rtmp, srt, udp, mms, HLS and more. |
| `file://` URL | Converted to a local path. | As for a file. |
| Audio CD | `cdda://` (File > Open Audio CD) | One row. |

Two more points:

- **Natural order** comes from `mediaInFolder()`. It uses a `QCollator` with the English locale and numeric mode, so `ep2` sorts before `ep10` whatever the system locale. The explicit locale is needed because the default collator sorted `ep10` before `ep2` on CI (a comment in `mediaInFolder()` records this).
- **Playlist entries** that are relative paths are resolved against the playlist's own folder.

The file dialog's filter lists the media extensions (from VLC 3.0's list, minus MIDI, SID and `.bin`, which libmpv can't play), the playlist extensions, and "All files". Choosing "All files" opens anything mpv can open.

## The open pipeline

`openFiles(files, append)`:

1. If `append` is false, clear the playlist.
2. Work through a queue, starting with the arguments. A playlist's entries are put at the front of the queue, so they are processed next, in order.
3. `seenLists` records each playlist already expanded, so a playlist that lists itself does not loop.
4. Each resolved item becomes a `QListWidgetItem` with three data roles (below).
5. Play the first new item. This happens when `append` is false, or when `append` is true and nothing is playing.

### Item data

Each row carries three roles:

| Role | Holds | Used for |
|---|---|---|
| `Qt::UserRole` | The URL that mpv loads: a path, a URL, `dvd://`, `bd://` or `cdda://` | `loadfile` |
| `Qt::UserRole + 1` | The source the user opened: the path, or the URL | Recent files, bookmarks, favourites, saved playlists, MPRIS metadata, section cutting, previews, the row's tooltip |
| `Qt::UserRole + 2` | True for the playing row | `syncCurrent()` finds `current` from it after the list changes |

The distinction between roles 0 and 1 matters for discs. A DVD row's `UserRole` is `dvd://`, and its source is the folder or image path, which `playIndex()` passes to `dvd-device`.

The playing row is shown in bold.

## Playing one row

`playIndex(i)` does these things, in this order:

1. Bold the row and set `UserRole + 2` on all rows.
2. `savePosition()`, which writes the resume file of the file that is leaving.
3. Set `stopped` to false.
4. For a disc row, set `dvd-device` or `bluray-device` to the source.
5. Reset `video-crop`. Crop is per file. The saved crop is applied again on `FILE_LOADED`.
6. Stop the recording, if one is running.
7. Clear the seek-preview cache and hide the preview.
8. `loadfile`.
9. `addRecent()`.
10. If exactly one local file is open, find the previous and next files in its folder (`folderNeighbors`).
11. Set `pause` to `no`.
12. Set the window title to the row's text followed by " - Reelplay".

Playing starts with `pause` set to `no`, which is why the file starts at once even after a pause.

## Playlist editing

The playlist is a `QListWidget` in a dock on the right (Ctrl+7 to show it).

| Action | How |
|---|---|
| Play a row | Double-click. |
| Reorder | Drag. The list's drag mode is `InternalMove`. |
| Remove | Delete, or the context menu. Removes the selected rows. |
| Randomise | Context menu. Shuffles all rows with `QRandomGenerator`. |
| Save as M3U | Context menu, or the playlist's Save shortcut. Writes `#EXTM3U` and each row's source. |
| Clear | Context menu. |

The model's `rowsMoved`, `rowsRemoved`, `rowsInserted` and `modelReset` signals all call `syncCurrent()`. This keeps the playing row marked after any edit.

The playlist actions use `Qt::WidgetShortcut`, so Delete only works while the list has focus. That is why Delete is not on the Options Keys tab (see [UI and menus](UI-and-Menus.md#hotkeys)).

## Next, previous and folder walking

`skipFile(dir)` does one of two things:

- If the playlist has exactly one row and it has a neighbour in its folder (`folderNeighbors[dir > 0]`), it opens that neighbour. This replaces the playlist with the neighbour, so the walk continues from it.
- Otherwise it plays `current + dir`.

`canSkip(dir)` answers the same question without acting. The MPRIS `CanGoNext` and `CanGoPrevious` properties use it. The toolbar buttons do not check it: at the ends of the list they do nothing.

The folder walk applies only to a single local file. A URL, a disc or a multi-row playlist does not get neighbours.

## End of file

When mpv reports `MPV_EVENT_END_FILE`, `handleEvents()` decides what to do:

1. If the reason is an error, show `Cannot play: <mpv's message>` in the status bar for 5 seconds.
2. If the reason is end of file (EOF) or an error:
   1. If another row follows, play it.
   2. Otherwise, if repeat is "playlist" and the reason is EOF, play row 0.
   3. Otherwise, if the reason is EOF, the Auto Next option is on, and `canSkip(1)` is true, play the next file in the folder.
   4. Otherwise, call `stop()`. If the reason was EOF, also run the After Playback action (`finishPlaylist()`).
3. Any other reason (the user pressed Stop, mpv quit, a redirect) is ignored. `stop()` has already run in those cases.

Two details follow from this:

- A file that **fails** to load does not trigger After Playback. The code says so: "a file that failed to load is not 'finished'".
- Repeat File never reaches step 2, because mpv loops the file itself (`loop-file=inf`).

## After playback

`finishPlaylist()` uses the `afterPlayback` setting:

| Value | Action |
|---|---|
| `nothing` | Nothing. The default. |
| `exit` | `close()`, queued with a zero timer so it runs outside mpv's event handling. |
| `poweroff` | Calls `PowerOff(true)` on logind's `Manager` over the system bus. |
| `suspend` | Calls `Suspend(true)` on the same interface. |

The `true` is "interactive": polkit may ask for a password. The call is non-blocking, because logind answers once and there is nothing to wait for. See [Desktop integration](Desktop-Integration.md#power-actions).

## Repeat

Off, Repeat File and Repeat Playlist, in the Play menu. The choice is saved as `repeat`.

- **Repeat File** sets mpv's `loop-file` to `inf`. mpv loops the file itself.
- **Repeat Playlist** is done by the app. When the last row ends at EOF, it plays row 0.

## Recent files

Each `playIndex()` puts its source at the top of the `recent` setting. The list keeps the 10 most recent, with no duplicates. File > Recent Files lists them, and Clear list empties the setting. Recent entries are sources, so a URL can be reopened from the menu.

## Saved playlists

File > Save Playlist (or the playlist's Save shortcut) writes an M3U file with `#EXTM3U` and one source per row. The source is the path or URL the user opened, not the internal URL. Loading it again goes through the same `readPlaylist()` path, so relative paths in it resolve against its own folder.

Reelplay saves M3U only. It reads the other playlist formats but does not write them.
