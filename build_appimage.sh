#!/bin/sh
set -eu

WITH_DJVU=${WITH_DJVU:-off}
WITH_SYNCTEX=${WITH_SYNCTEX:-on}
WITH_LUA=${WITH_LUA:-on}
WITH_LIBRSVG=${WITH_LIBRSVG:-off}

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build-appimage"
APPDIR="$ROOT_DIR/appimage/AppDir"

DESKTOP_FILE="$APPDIR/usr/share/applications/lektra.desktop"
ICON_FILE="$APPDIR/usr/share/icons/hicolor/512x512/apps/lektra.png"
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
CLEAN_APPDIR=${CLEAN_APPDIR:-1}
APP_VERSION=${APP_VERSION:-$(sed -n 's/^[[:space:]]*project(lektra VERSION \([0-9.]*\).*/\1/p' "$ROOT_DIR/CMakeLists.txt" | head -n 1)}

if [ -z "$APP_VERSION" ]; then
    APP_VERSION="dev"
fi

need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "Error: missing required tool: $1" >&2
        exit 1
    }
}

need cmake
need make

if [ "$CLEAN_APPDIR" -eq 1 ]; then
    rm -rf "$APPDIR"
fi
mkdir -p "$APPDIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DWITH_LUA="$WITH_LUA" \
    -DWITH_SYNCTEX="$WITH_SYNCTEX" \
    -DWITH_DJVU="$WITH_DJVU" \
    -DWITH_LIBRSVG="$WITH_LIBRSVG"

cmake --build "$BUILD_DIR" -j"$JOBS"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

if [ ! -x "$APPDIR/usr/bin/lektra" ]; then
    echo "Error: expected binary not found: $APPDIR/usr/bin/lektra" >&2
    exit 1
fi

if [ -f "$DESKTOP_FILE" ]; then
    sed -i 's|^Exec=.*|Exec=lektra %f|' "$DESKTOP_FILE"
else
    echo "Error: desktop file missing: $DESKTOP_FILE" >&2
    exit 1
fi

ln -sf usr/bin/lektra "$APPDIR/AppRun"

export EXTRA_QT_PLUGINS="waylandcompositor;imageformats"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export QMAKE=qmake6
export APPIMAGE_EXTRACT_AND_RUN=1

VERSION="$APP_VERSION" NO_STRIP=0 \
	./linuxdeploy-x86_64.AppImage \
	--appdir "$APPDIR" \
	--executable ./appimage/AppDir/usr/bin/lektra \
	--desktop-file ./appimage/AppDir/usr/share/applications/lektra.desktop \
	--icon-file ./appimage/AppDir/usr/share/icons/hicolor/512x512/apps/lektra.png \
	--plugin qt \
	--output appimage
