# History

Versions and what changed, from the project's commit history and releases. Commit hashes are the short form.

## Releases

| Version | Name | Notes |
|---|---|---|
| 0.3.1 | newmpc-be | The last release under the old name. Tag `v0.3.1` on `main`, released 2026-10-15. |
| 0.4.0 | Reelplay | Renamed. Single window, chapters, Go To, subtitle options (`c39abcc`). |
| 0.5.0 | Reelplay | Always on top, compact views, statistics, video and audio options (`104048c`). |
| 0.7.0 | Reelplay | Equaliser, visualiser, A-B sections, screenshot series. Includes the 0.6.0 features; there is no separate 0.6.0 commit (`52a0ee1`). |
| 0.8.0 | Reelplay | Pitch, seek-bar previews, cover art, CI, and the AppImage script (`4511f6b`). Published as a GitHub release with the `.deb` on 2026-11-17, targeting `9c98dbd`. |

## Commits since 0.8.0

In the order they were made:

| Commit | Change |
|---|---|
| `e2e40b2` | CI: null audio output for the tests, and a window manager on the virtual display. |
| `54c3d2e` | CI hang fixed: null video output for the tests. Crop reads the source size when needed. |
| `cd75af3` | Folder playlists sorted with an explicit English collator. |
| `a70a1b8` | `checkout` and `upload-artifact` moved to v7, which runs on Node 24. |
| `9c98dbd` | The `recordStream` test waits for the asynchronous `stream-record` set. |
| `3da7c4f` | README rewritten by topic, with screenshots. |
| `aac1739` | Visualiser fix: audio and video tracks are handed back when the visualiser is cleared, so playback no longer stalls. |

## Notes

- The version number lives in `CMakeLists.txt`. See [CI and release](CI-and-Release.md#release) for the steps to make a new one.
- Before 0.4.0, the project was `newmpc-be`. Settings and resume files move to the new names on first launch. See [Settings and data](Settings-and-Data.md#migration-from-newmpc-be).
