# UI and menus

The window's layout, its menus, how hotkeys work and which of them can be changed, and the Options dialog.

## Window layout

From top to bottom:

1. **Menu bar:** File, View, Play, Favorites, Help. Hideable (Ctrl+0).
2. **Video area:** at least 320 × 180, black background. Mouse, wheel and right-click events go to `eventFilter()`.
3. **Controls** (hideable with Ctrl+1):
   - the seek bar, with chapter ticks and frame previews on hover;
   - a row of buttons: Play, Pause, Stop, Previous, Next, a stretch, Mute, and a volume slider 90 px wide.
4. **Status bar** (hideable with Ctrl+2):
   - the status label: Stopped, Opening..., Playing or Paused, or a message that stays for a few seconds;
   - a permanent time label, `hh:mm:ss / hh:mm:ss`.

The **playlist** is a dock on the right, hidden by default and shown with Ctrl+7. It can be moved and floated like any Qt dock.

The default size is 960 × 600. A saved geometry replaces it.

The buttons and the volume slider have no keyboard focus (`Qt::NoFocus`), so they do not take keys meant for the video.

## Menus

| Menu | Contents |
|---|---|
| **File** | Open File (Ctrl+O), Open URL (Ctrl+U), Open Folder, Open DVD/BD (Ctrl+D), Open Audio CD, Recent Files, Add to Playlist, Load Subtitle (Ctrl+L), Save Image (Alt+I), Save Image (Auto) (F5), Screenshot Series, Properties (Shift+F10), Close (Ctrl+C), Exit (Alt+X). |
| **View** | Playlist (Ctrl+7), Fullscreen (Alt+Return), Exit Fullscreen (Esc), Options (O), Zoom (Alt+1, Alt+2, Alt+3), Aspect Ratio, Always on Top (Ctrl+A), Hide Menu Bar (Ctrl+0), Hide Controls (Ctrl+1), Hide Status Bar (Ctrl+2), Statistics (Ctrl+J), Visualizer (audio files), Video (see [Video features](Video-Features.md)). |
| **Play** | Play/Pause (Space), Stop (.), Frame Step (Ctrl+Right, Ctrl+Left), jumps (arrows, Shift+arrows), Go To (Ctrl+G), Playback Rate, Sync, A-B Repeat, Repeat, Bookmarks, Record Stream, After Playback, Next File (PgDn), Previous File (PgUp), Chapters, Skip Intro / Credits (S), Audio Track, Subtitle Track, Volume, Audio Output. |
| **Favorites** | Add Current File (Ctrl+Shift+F), then the favourites list, then Clear Favorites. |
| **Help** | About. |

The context menu, on right-click over the video, is made from the menu bar's menus when the window is built. It always matches the menu bar.

The feature details are in [Playback features](Playback-Features.md), [Video features](Video-Features.md) and [Audio features](Audio-Features.md). The keys themselves are in the [README](../../README.md#keyboard-shortcuts).

## Hotkeys

A hotkey is a `QAction` shortcut. Reelplay adds every action to the window itself (`addAction`), so its shortcut works even when the menu bar is hidden. Three kinds of action exist:

| Kind | Made by | Remappable? |
|---|---|---|
| Menu items | `Player::act()` | Yes. The object name is `Menu/Item`. |
| Checkable items (Always on Top, the three Hide items, Statistics, Mute, Flip, Denoise, and similar) | The `checkable()` helper in the constructor | **No.** They have no object name. |
| Other actions | Built directly in the constructor: Ctrl+B bookmark, Ctrl+Shift+F favourite, the media keys, the playlist's Delete, Save and similar | **No.** |

### How remapping works

`act()` names each action `<menu title>/<item text>`, with `&` removed from both parts and `/` in the item text replaced by `-`. So "Play" > "&Play/Pause" becomes `Play/Play-Pause`. That name is the action's stable id.

The Options > Keys tab lists every action whose object name contains `/`. Only the first key of a chord is kept. Saved keys are stored under `keys/<name>` (see [Settings and data](Settings-and-Data.md)), and only when they differ from the default. `applySettings()` applies them at start-up.

The default key of each action is stored as the `defaultKey` property. "Reset All to Defaults" restores those.

**The README says every shortcut can be changed in Options. That is not true:** the checkable items, the other direct actions listed above, and the playlist's keys cannot be changed. The README should say so.

### Playlist keys

The playlist's actions (Remove with Delete, Randomise, Save, Clear) use `Qt::WidgetShortcut`. They only fire while the playlist has focus, so Delete does nothing in the video.

## Options dialog

View > Options (O). Three tabs. OK saves everything and calls `applySettings()`. Cancel discards the changes.

| Tab | Controls |
|---|---|
| **Player** | Hardware decoding (`no`, `auto-safe`, `vaapi`, `nvdec`, `vdpau`); Preferred audio language and Preferred subtitle language; Screenshot folder (its placeholder shows the default); Dark theme; Use a single window (takes effect at the next launch); When a file ends, play the next file in its folder. |
| **Subtitles** | Font, size, colour, vertical position. |
| **Keys** | A table of the remappable actions, with a key editor for each, and Reset All to Defaults. |

Most controls have an object name matching their setting key (for example `hwdec`, `singleInstance`, `autoNext`). The tests use these names to find the controls.

## Context and popups

- **Dialogs** that the app opens (Adjust Colors, Equalizer, Options, Go To, Screenshot Series and the rest) are modal and run `exec()`. Tests drive them through `onModal()`.
- **Tooltips** show the time over the seek bar and the full path of a playlist row.
- **Messages** that need no decision go to the status bar, or to the OSD (on-screen display) in the video, as `show-text`.

## Dark theme

Options > Player > Dark theme. See [Video features](Video-Features.md#dark-theme), including the known issue with switching it off.
