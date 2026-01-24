#!/bin/sh
set -eu

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build-deb"
STAGE_DIR="$ROOT_DIR/_deb_stage"
PKG_DIR="$ROOT_DIR/_deb_pkg"
OUT_DIR="$ROOT_DIR/dist"

APP_NAME="lektra"
APP_VERSION=${APP_VERSION:-$(sed -n 's/^[[:space:]]*project(lektra VERSION \([0-9.]*\).*/\1/p' "$ROOT_DIR/CMakeLists.txt" | head -n 1)}
if [ -z "$APP_VERSION" ]; then
    APP_VERSION="dev"
fi

ARCH=${ARCH:-$(command -v dpkg >/dev/null 2>&1 && dpkg --print-architecture || uname -m)}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
BUILD_TYPE=${BUILD_TYPE:-Release}
ENABLE_LLM_SUPPORT=${ENABLE_LLM_SUPPORT:-OFF}
CLEAN_BUILD=${CLEAN_BUILD:-0}

MUPDF_LIB="$ROOT_DIR/external/mupdf/build/release/libmupdf.a"
MUPDF_THIRD_LIB="$ROOT_DIR/external/mupdf/build/release/libmupdf-third.a"

need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "Error: missing required tool: $1" >&2
        exit 1
    }
}

need cmake
need make
need dpkg-deb

if [ ! -f "$MUPDF_LIB" ] || [ ! -f "$MUPDF_THIRD_LIB" ]; then
    if [ ! -d "$ROOT_DIR/external/mupdf" ]; then
        echo "Error: external/mupdf missing. Run ./install.sh --rebuild-mupdf to fetch it." >&2
        exit 1
    fi
    echo "MuPDF libs missing; building MuPDF..."
    make -C "$ROOT_DIR/external/mupdf" build=release HAVE_X11=no HAVE_GLUT=no
fi

if [ "$CLEAN_BUILD" -eq 1 ]; then
    rm -rf "$BUILD_DIR"
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DENABLE_LLM_SUPPORT="$ENABLE_LLM_SUPPORT"
cmake --build "$BUILD_DIR" -j"$JOBS"

rm -rf "$STAGE_DIR" "$PKG_DIR"
DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR"

mkdir -p "$PKG_DIR/DEBIAN"
cp -a "$STAGE_DIR"/. "$PKG_DIR"/

INSTALLED_SIZE=$(du -ks "$PKG_DIR/usr" 2>/dev/null | awk '{print $1}')

cat >"$PKG_DIR/DEBIAN/control" <<EOF
Package: $APP_NAME
Version: $APP_VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: Dheeraj Vittal Shenoy <dheerajshenoy22@gmail.com>
Homepage: https://codeberg.org/lektra/lektra
Installed-Size: ${INSTALLED_SIZE:-0}
Build-Depends: cmake ninja-build
Depends: qt6-base-dev, curl, libsynctex-dev
Suggests: qt6-style-kvantum
Description: A fast, keyboard-based, configurable PDF reader
EOF

mkdir -p "$OUT_DIR"
OUT_FILE="$OUT_DIR/${APP_NAME}_${APP_VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$PKG_DIR" "$OUT_FILE"

echo "Built: $OUT_FILE"
