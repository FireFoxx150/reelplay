#!/usr/bin/env bash
# Builds Reelplay as an AppImage. Run from anywhere: ./packaging/appimage/build-appimage.sh
#
# It downloads linuxdeploy and its Qt plugin from GitHub's "continuous" releases, which
# are not pinned by checksum. Run it only if you accept those downloads. Needs qmake6
# (qt6-base-dev-tools) and libmpv-dev on the build machine.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/appimage"
rm -rf "$OUT"
mkdir -p "$OUT/tools"

cmake -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$ROOT/build" -j
DESTDIR="$OUT/AppDir" cmake --install "$ROOT/build" --prefix /usr

curl -fL -o "$OUT/tools/linuxdeploy" \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
curl -fL -o "$OUT/tools/linuxdeploy-plugin-qt" \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x "$OUT/tools/linuxdeploy" "$OUT/tools/linuxdeploy-plugin-qt"

cd "$OUT"
# linuxdeploy bundles the Qt libraries (via the Qt plugin) and libmpv with its dependencies.
ARCH=x86_64 ./tools/linuxdeploy \
  --appdir AppDir \
  --executable AppDir/usr/bin/reelplay \
  --desktop-file AppDir/usr/share/applications/reelplay.desktop \
  --icon-file AppDir/usr/share/icons/hicolor/scalable/apps/reelplay.svg \
  --plugin qt \
  --output appimage

echo "AppImage written to: $(ls "$OUT"/Reelplay*.AppImage 2>/dev/null || echo "$OUT (check the output above)")"
