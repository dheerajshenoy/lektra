#!/bin/sh
set -eu

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build-appimage"
APPDIR="$ROOT_DIR/appimage/AppDir"
MUPDF_LIB="$ROOT_DIR/external/mupdf/build/release/libmupdf.a"
MUPDF_THIRD_LIB="$ROOT_DIR/external/mupdf/build/release/libmupdf-third.a"
DESKTOP_FILE="$APPDIR/usr/share/applications/lektra.desktop"
ICON_FILE="$APPDIR/usr/share/icons/hicolor/256x256/apps/lektra.png"
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
need linuxdeploy

if [ ! -f "$MUPDF_LIB" ] || [ ! -f "$MUPDF_THIRD_LIB" ]; then
    if [ ! -d "$ROOT_DIR/external/mupdf" ]; then
        echo "Error: external/mupdf missing. Run ./install.sh --rebuild-mupdf to fetch it." >&2
        exit 1
    fi
    echo "MuPDF libs missing; building MuPDF..."
    make -C "$ROOT_DIR/external/mupdf" build=release HAVE_X11=no HAVE_GLUT=no
fi

if [ "$CLEAN_APPDIR" -eq 1 ]; then
    rm -rf "$APPDIR"
fi
mkdir -p "$APPDIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
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

VERSION="$APP_VERSION" NO_STRIP=1 linuxdeploy \
    --appdir "$APPDIR" \
    --desktop-file "$DESKTOP_FILE" \
    --icon-file "$ICON_FILE" \
    --output appimage
