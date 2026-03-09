#!/bin/sh
set -eu

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build-macos"
DIST_DIR="$ROOT_DIR/dist"
ICONSET_DIR="$BUILD_DIR/lektra.iconset"
ICNS_FILE="$BUILD_DIR/lektra.icns"
APP_BUNDLE="$BUILD_DIR/lektra.app"
DMG_ROOT="$BUILD_DIR/dmg-root"
APP_NAME="lektra"
APP_VERSION=${APP_VERSION:-$(sed -n 's/^[[:space:]]*project(lektra VERSION \([0-9.]*\).*/\1/p' "$ROOT_DIR/CMakeLists.txt" | head -n 1)}
ARCH=${ARCH:-$(uname -m)}
JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 1)}
QT_PREFIX=${QT_PREFIX:-}

if [ -z "$APP_VERSION" ]; then
    APP_VERSION="dev"
fi

need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "Error: missing required tool: $1" >&2
        exit 1
    }
}

resolve_qt_prefix() {
    if [ -n "$QT_PREFIX" ]; then
        return
    fi

    if command -v brew >/dev/null 2>&1; then
        QT_PREFIX=$(brew --prefix qt 2>/dev/null || true)
    fi

    if [ -z "$QT_PREFIX" ] && command -v qmake6 >/dev/null 2>&1; then
        QT_PREFIX=$(qmake6 -query QT_INSTALL_PREFIX 2>/dev/null || true)
    fi

    if [ -z "$QT_PREFIX" ] && command -v qmake >/dev/null 2>&1; then
        QT_PREFIX=$(qmake -query QT_INSTALL_PREFIX 2>/dev/null || true)
    fi

    if [ -z "$QT_PREFIX" ]; then
        echo "Error: could not determine Qt installation prefix." >&2
        exit 1
    fi
}

copy_icon() {
    mkdir -p "$ICONSET_DIR"

    cp "$ROOT_DIR/resources/hicolor/16x16/apps/lektra.png" "$ICONSET_DIR/icon_16x16.png"
    cp "$ROOT_DIR/resources/hicolor/32x32/apps/lektra.png" "$ICONSET_DIR/icon_16x16@2x.png"
    cp "$ROOT_DIR/resources/hicolor/32x32/apps/lektra.png" "$ICONSET_DIR/icon_32x32.png"
    cp "$ROOT_DIR/resources/hicolor/64x64/apps/lektra.png" "$ICONSET_DIR/icon_32x32@2x.png"
    cp "$ROOT_DIR/resources/hicolor/128x128/apps/lektra.png" "$ICONSET_DIR/icon_128x128.png"
    cp "$ROOT_DIR/resources/hicolor/256x256/apps/lektra.png" "$ICONSET_DIR/icon_128x128@2x.png"
    cp "$ROOT_DIR/resources/hicolor/256x256/apps/lektra.png" "$ICONSET_DIR/icon_256x256.png"
    cp "$ROOT_DIR/resources/hicolor/512x512/apps/lektra.png" "$ICONSET_DIR/icon_256x256@2x.png"
    cp "$ROOT_DIR/resources/hicolor/512x512/apps/lektra.png" "$ICONSET_DIR/icon_512x512.png"
    sips -z 1024 1024 "$ROOT_DIR/resources/hicolor/512x512/apps/lektra.png" --out "$ICONSET_DIR/icon_512x512@2x.png" >/dev/null

    iconutil -c icns "$ICONSET_DIR" -o "$ICNS_FILE"

    mkdir -p "$APP_BUNDLE/Contents/Resources"
    cp "$ICNS_FILE" "$APP_BUNDLE/Contents/Resources/lektra.icns"
}

if [ "$(uname -s)" != "Darwin" ]; then
    echo "Error: build_dmg.sh only works on macOS." >&2
    exit 1
fi

need cmake
need macdeployqt
need hdiutil
need iconutil
need sips
need codesign
need ditto

resolve_qt_prefix

mkdir -p "$DIST_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build "$BUILD_DIR" -j"$JOBS"

if [ ! -d "$APP_BUNDLE" ]; then
    echo "Error: expected app bundle not found: $APP_BUNDLE" >&2
    exit 1
fi

copy_icon

rm -f "$BUILD_DIR"/*.dmg "$DIST_DIR"/*.dmg
rm -rf "$DMG_ROOT"

macdeployqt "$APP_BUNDLE" -always-overwrite
codesign --force --deep --sign - "$APP_BUNDLE"

mkdir -p "$DMG_ROOT"
ditto "$APP_BUNDLE" "$DMG_ROOT/$APP_NAME.app"
ln -s /Applications "$DMG_ROOT/Applications"

DMG_FILE="$BUILD_DIR/${APP_NAME}.dmg"
hdiutil create -volname "Lektra" -srcfolder "$DMG_ROOT" -ov -format UDZO "$DMG_FILE"

if [ -z "$DMG_FILE" ]; then
    echo "Error: macdeployqt did not produce a dmg in $BUILD_DIR" >&2
    exit 1
fi

OUT_FILE="$DIST_DIR/${APP_NAME}-${APP_VERSION}-${ARCH}.dmg"
mv -f "$DMG_FILE" "$OUT_FILE"

echo "Built: $OUT_FILE"
