# CI and release

How Reelplay is checked on every push, how the Debian package is built, and how a release is made. The workflow is `.github/workflows/ci.yml`.

## The CI workflow

Every push and every pull request runs one job, `build-and-test`, on `ubuntu-24.04`. The Ubuntu version matters: 24.04 ships Qt 6.4 and mpv 0.37, the versions Reelplay is developed against.

| Step | What it does |
|---|---|
| Checkout | `actions/checkout@v7`. |
| Install dependencies | `apt-get install` of `cmake`, `pkg-config`, `qt6-base-dev`, `libgl-dev`, `libgl1-mesa-dri`, `libmpv-dev`, `xvfb`, `openbox`, `x11-utils`, `dbus-x11`, `libglib2.0-bin` and `ffmpeg`. |
| Configure and build | `cmake -B build -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build -j`. |
| Run tests under a virtual display | Runs `./build/test_player` under `xvfb-run` and `dbus-run-session`, with openbox started first. Environment: `LIBGL_ALWAYS_SOFTWARE=1`, `REELPLAY_AO=null`, `REELPLAY_VO=null`. |
| Build the Debian package | `cmake --build build --target package`. |
| Upload the artifact | `actions/upload-artifact@v7` uploads `build/*.deb` as `reelplay-deb`. |

Things to know about the workflow:

- **ffmpeg is installed**, so the tests that need it run rather than skip.
- **Both outputs are null.** Nothing in CI hears or sees playback. The tests check mpv's state instead. See [Testing](Testing.md#environment).
- **The locale is the runner's default.** Folder sorting uses an explicit English collator, because a comment in the code records that the default sorted `ep10` before `ep2` on CI.
- **Actions run on Node 24.** `checkout` and `upload-artifact` moved to v7 in `a70a1b8`, which runs on Node 24 and clears GitHub's warning about the older Node version.
- **The artifact is the package from the same run**, so a `.deb` from a green run is what the tests ran against.

## Reading a failed run

The step name says which stage failed:

- **Install dependencies:** a package is missing or renamed on the runner image.
- **Configure and build:** a compile or link error. The log has the compiler output.
- **Run tests:** a test failed. QtTest prints `FAIL!` with the file and line, and the totals.
- **Build the Debian package:** CPack failed, usually a problem in `CMakeLists.txt`.

From a terminal:

```bash
gh run list --repo FireFoxx150/reelplay --branch main --limit 5
gh run view <run-id> --repo FireFoxx150/reelplay --log-failed
```

## Reproducing CI locally

Run the same commands as the workflow. The test step is the one in [Testing](Testing.md#running). Use the same environment variables. If a sorting test fails, check the locale: `locale` shows the current one, and `LC_ALL=<locale>` sets it for one run.

## Release

A release is a GitHub release with the Debian package attached. There is no release script; the steps below are the ones used for 0.8.0.

1. **Bump the version** in `CMakeLists.txt`, in `project(reelplay VERSION x.y.z ...)`. This sets the package file name and the version recorded in it. Also update the install line in the [README](../../README.md#debian-and-ubuntu-package), which names the `.deb` file.
2. **Commit and push.** Wait for CI to pass on that commit.
3. **Build the package** from that commit:
   ```bash
   cmake --build build --target package
   ```
4. **Try it.** Install the built package and start the app:
   ```bash
   sudo apt install ./build/reelplay_x.y.z_amd64.deb
   ```
5. **Create the release.** Use the **full** commit SHA for `--target`. A short SHA was rejected when the 0.8.0 release was created.
   ```bash
   gh release create vX.Y.Z build/reelplay_X.Y.Z_amd64.deb \
       --repo FireFoxx150/reelplay \
       --target <full commit SHA> \
       --title "Reelplay X.Y.Z" \
       --notes "<what changed>"
   ```
6. **Check the release page** lists the `.deb`. `gh release create` makes the tag on GitHub; run `git fetch --tags` to get it locally.

The app's version is only in `CMakeLists.txt`. The About dialog shows the Qt version, not the app version.

The version history is in [History](History.md).

### Previous name

The last release under the old name, `newmpc-be` 0.3.1, is the tag `v0.3.1`. Reelplay's packages replace and conflict with `newmpc-be`, so installing one removes the other.

## AppImage

`packaging/appimage/build-appimage.sh` builds an AppImage locally. It is **not** part of CI and **not** part of a release. It downloads linuxdeploy and its Qt plugin from GitHub's "continuous" releases without checksums. See [Building and running](Building-and-Running.md#appimage).
