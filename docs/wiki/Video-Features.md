# Video features

Everything under View > Video, the window and fullscreen behaviour, and the dark theme. The mpv properties behind each item are listed in [mpv integration](mpv-Integration.md#runtime-settings).

## View > Video

| Item | What it does | Saved as |
|---|---|---|
| Adjust Colors... | A dialog with five sliders, −100 to 100: brightness, contrast, saturation, gamma, hue. Each one sets its mpv property as it moves. Reset sets them all to 0. | `video/brightness`, `video/contrast`, `video/saturation`, `video/gamma`, `video/hue` |
| Rotate Clockwise, Rotate Counter-clockwise | `video-rotate` changes by ±90, wrapping within 0 to 359. | `video/rotate` |
| Flip Horizontal | Adds or removes the labelled filter `@flip` (`hflip`). | `video/flip` |
| Deinterlace | `deinterlace` yes or no. | `video/deinterlace` |
| Crop | No Crop, Crop 4:3, 16:9, 1.85:1 or 2.35:1. See [Crop](#crop). | `video/crop` (the aspect ratio as a number, 0 for none) |
| Scaling | Default (bilinear), Sharp (`ewa_lanczossharp`), Smooth (`spline36`). Sets mpv's `scale`. | `scale` |
| HDR Tone Mapping | `tone-mapping` auto or clip. On by default. | `toneMap` |
| Denoise | Adds or removes the labelled filter `@denoise` (`hqdn3d`). | `video/denoise` |
| Sharpen | Adds or removes the labelled filter `@sharpen` (`unsharp`). | `video/sharpen` |

**View > Aspect Ratio** sets `video-aspect-override` to Default, 4:3, 16:9, 1.85:1 or 2.35:1. It is **not saved**: the choice lasts until the next launch. This is different from Crop, which is.

### Crop

`setCrop(aspect)` computes a centred crop with `video-crop`, as `WxH+X+Y`:

- If the source is wider than the target ratio, the width is reduced to `height × aspect`.
- Otherwise the height is reduced to `width / aspect`.
- The offsets centre the crop.

The source size is `srcSize`, captured on `FILE_LOADED`. If that was missing (the video output came up late), `setCrop()` asks mpv again.

Crop applies to every file, but it is reset per file: `playIndex()` clears `video-crop`, and the `FILE_LOADED` handler applies the saved aspect again and ticks the matching menu item.

### Rotation, flip, deinterlace, denoise and sharpen

These are remembered and re-applied at start-up by `applySettings()`. The labelled filters are the reason re-applying is safe: `setVideoFilter()` always removes the old copy before adding one, so the same settings applied twice do not produce two filters.

Denoise and sharpen cost CPU. Denoise (`hqdn3d`) is the heavier of the two on large files.

## Window

| Item | Shortcut | Behaviour |
|---|---|---|
| Always on Top | Ctrl+A | Sets `Qt::WindowStaysOnTopHint` on the `QWindow`. The state is not saved: it is off at each launch. |
| Hide Menu Bar | Ctrl+0 | Hides the menu bar. |
| Hide Controls | Ctrl+1 | Hides the seek bar and buttons. |
| Hide Status Bar | Ctrl+2 | Hides the status bar. |

The three Hide items are per session, not saved. Their state is kept in `hideMenu`, `hideControls` and `hideStatus`, and `applyChrome()` checks them. Entering fullscreen and leaving it does not change them, so something the user hid stays hidden.

Always on Top is set on the `QWindow`, not the widget. The reason is in the code: recreating the widget's window would orphan mpv's native video window on X11. See [mpv integration](mpv-Integration.md#video-output).

The three Hide items are on the context menu too, because the context menu is built from the menu bar. Their shortcuts are added to the window directly, so they still work when the menu bar is hidden.

## Fullscreen

- **Enter:** Alt+Enter (listed as Alt+Return in the menu), or double-click the video.
- **Leave:** Alt+Enter again, Esc, or double-click again.

What happens on entry:

1. `applyChrome(true)` hides the menu bar, status bar and controls. Parts the user had hidden stay hidden.
2. The playlist dock is hidden, and its visibility is remembered in a static variable.
3. The window goes fullscreen.
4. `idleTimer` starts. After two seconds without mouse movement the cursor is hidden and the controls are hidden, unless the pointer is over them.

While fullscreen, moving the mouse within 40 px of the bottom of the video shows the controls, and moving away hides them again. Controls the user hid stay hidden.

Leaving restores the menu bar, status bar and controls, and the dock's previous visibility.

Closing the window in fullscreen first leaves fullscreen (`closeEvent()`), so the saved geometry is the windowed one.

A single click on the video toggles pause, but only after the double-click interval has passed. This is what stops a double-click from also pausing: the click timer is cancelled by the second click.

Tests: `fullscreenHotkeys`, `fullscreenAutoControls`, `hiddenPartsSurviveFullscreen`, `doubleClickOnlyFullscreens`, `singleClickPauses`.

## Dark theme

Options > Player > Dark theme (on by default).

- **On:** `applyTheme(true)` switches to the Fusion style and applies a dark palette.
- **Off:** `applyTheme(false)` restores the palette the application had at first launch, which was captured in a static variable the first time `applyTheme()` ran.

**Known issue:** switching the theme off restores the palette but **not the style**. The Fusion style stays until the next launch. This comes from reading the code: `applyTheme(false)` only calls `setPalette`. It has not been reproduced in a session. See [Troubleshooting](Troubleshooting.md#switching-the-dark-theme-off-keeps-the-fusion-style).

## Video output modes

The window draws video in one of two ways: embedded in a native window on X11, or through the render API on Wayland and when `REELPLAY_RENDER=gl` is set. The user sees the same features either way. The differences are in start-up time and in how always-on-top and keyboard input work, all covered in [mpv integration](mpv-Integration.md#video-output).

## Tests

Colour and transforms: `videoAdjustments`, `videoTransforms`, `videoFilters`, `videoChoicesPersist`. Aspect and zoom: `aspectRatio`, `zoomHotkeys`. Window and chrome: `alwaysOnTop`, `chromeToggles`.
