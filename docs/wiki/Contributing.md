# Contributing

The repository has no CONTRIBUTING file. This page sets out the conventions that the code and the history show, and checklists for the common kinds of change.

## Ground rules

- **Keep changes small.** One behaviour per commit. Leave unrelated code, comments and formatting alone.
- **Match the code around you**, even where you would write it differently.
- **Add a test for each behaviour change.** For a bug, first write a test that fails on the bug, then fix it. `visualiserDoesNotStallPlayback` is an example.
- **Keep CI green.** Every push runs the workflow. See [CI and release](CI-and-Release.md).
- **Write commit messages as a short sentence**, in the imperative mood, with no prefix. The history has examples: "Sort folder playlists with an explicit English collator", "Wait for the async stream-record set in recordStream". Use the body for the reason, as the visualiser fix does.
- **Don't commit build output.** `build/` and `graphify-out/` are ignored.

## Code conventions

These come from the existing code:

- **Language:** C++17 and Qt 6. Qt types and naming (`QString`, `QListWidget`, camelCase methods). Members have no prefix or suffix.
- **Indentation:** four spaces.
- **Comments say why.** Most comments in `player.cpp` explain a constraint: a mpv quirk, a locale, a shutdown order. Write one when a reader could reasonably change the code the wrong way.
- **Lambdas** for menu callbacks, connected to `this`, as the constructor does throughout.
- **Settings** are read with `QSettings()`, constructed where needed. The key names follow the existing pattern: `group/key` for grouped settings, camelCase for others.
- **mpv access** goes through the helpers: `command()` for commands, `prop()` for reads, `step()` for synchronous numeric changes. Use the direct mpv calls only where the helpers do not fit (start-up options, `af`).
- **Filters** are labelled (`vf add @label:filter`), so they can be removed and added again without stacking.

## Checklists

### Adding a menu command with a hotkey

1. Add it with `act(menu, "&Text", QKeySequence("..."), [this] { ... })` in the constructor, in the menu where users will look for it.
2. For a toggle, use `checkable(...)` instead. Note that checkable items **cannot be remapped** in Options. If that is not what you want, give the action an object name of the form `Menu/Item` yourself.
3. Add a test that triggers it by its text and checks mpv's state (`trigger("&Text")`).
4. Update the keyboard table in the [README](../../README.md#keyboard-shortcuts).
5. If the menu or item text changes, the saved hotkey (`keys/Menu/Item`) stops matching it. See [UI and menus](UI-and-Menus.md#how-remapping-works).

### Adding a setting

1. Choose a key. Group related keys with `/` (`video/...`, `eq/...`).
2. Read it where it is used, with a default. If the same default is needed in another place, use the same value there. The code repeats defaults, so keep them in step.
3. Save it when it changes: `QSettings().setValue(...)`.
4. Apply it in `applySettings()`, so it takes effect at start-up and after the Options dialog closes.
5. If the user should see it, add it to the Options dialog (with an object name equal to the key) or to a menu.
6. Add a test. `optionsApplyAndPersist` is the pattern: change the value, check mpv's state, and check the saved key.
7. Document it in [Settings and data](Settings-and-Data.md).

### Adding an mpv option

- **Fixed at start-up** (devices, paths, the video output): call `mpv_set_option_string` in the constructor, before `mpv_initialize()`.
- **Changed while running**: call `mpv_set_property` or `command()` from the action. If it should survive a restart, also set it in `applySettings()`.
- Document it in [mpv integration](mpv-Integration.md).

### Changing playback handling

- A property read right after a `command()` can return the old value. React to the mpv event in `handleEvents()` where you can, or test with `QTRY_*`.
- For end-of-file behaviour, change the table in [Playback and files](Playback-and-Files.md#end-of-file) as well as the code.

### Changing the visualiser, filters or track selection

Read the track-selection section of [mpv integration](mpv-Integration.md#track-selection-and-the-visualiser) first. This is the area with a known mpv quirk, and the regression test for it must keep passing.

### Changing MPRIS

- The properties come from `Mpris::playerProperties()`. Add a value there, and to the matching `Q_PROPERTY` in `MprisPlayer`.
- `Mpris::update()` sends a `PropertiesChanged` for each value that differs from the last one, so no extra signal is needed for ordinary properties.
- Keep the specification's rules: ignore a stale `SetPosition`, and ignore a `Rate` of zero or less.
- Test it with `gdbus`, as `mprisControls` does.

### Changing the Options dialog

Each control's object name is its setting key, and `applySettings()` is the single place where saved settings reach mpv. A control that does not take effect after OK is usually missing from `applySettings()`.

## Pull requests

The history shows commits made straight to `main`. A pull request is still useful for a larger change, and CI runs on it as on any push.
