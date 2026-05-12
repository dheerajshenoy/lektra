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
WITH_DJVU=${WITH_DJVU:-OFF}
WITH_SYNCTEX=${WITH_SYNCTEX:-ON}
WITH_LUA=${WITH_LUA:-ON}
WITH_LIBRSVG=${WITH_LIBRSVG:-OFF}
CLEAN_BUILD=${CLEAN_BUILD:-0}

need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "Error: missing required tool: $1" >&2
        exit 1
    }
}

need cmake
need make
need dpkg-deb

if [ "$CLEAN_BUILD" -eq 1 ]; then
    rm -rf "$BUILD_DIR"
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DWITH_DJVU="$WITH_DJVU" \
    -DWITH_SYNCTEX="$WITH_SYNCTEX" \
    -DWITH_LUA="$WITH_LUA" \
    -DWITH_LIBRSVG="$WITH_LIBRSVG"
cmake --build "$BUILD_DIR" -j"$JOBS"

rm -rf "$STAGE_DIR" "$PKG_DIR"
DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR"

mkdir -p "$PKG_DIR/DEBIAN"
cp -a "$STAGE_DIR"/. "$PKG_DIR"/

INSTALLED_SIZE=$(du -ks "$PKG_DIR/usr" 2>/dev/null | awk '{print $1}')

_lower() { echo "$1" | tr '[:upper:]' '[:lower:]'; }
DJVU_DEP=$([ "$(_lower "$WITH_DJVU")"       = "on" ] && echo ", libdjvulibre-dev"  || true)
LUA_DEP=$([ "$(_lower "$WITH_LUA")"        = "on" ] && echo ", liblua5.4-dev"     || true)
LIBRSVG_DEP=$([ "$(_lower "$WITH_LIBRSVG")" = "on" ] && echo ", librsvg2-dev"     || true)

cat >"$PKG_DIR/DEBIAN/control" <<EOF
Package: $APP_NAME
Version: $APP_VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: Dheeraj Vittal Shenoy <dheerajshenoy22@gmail.com>
Homepage: https://codeberg.org/lektra/lektra
Installed-Size: ${INSTALLED_SIZE:-0}
Build-Depends: build-essential pkgconf cmake ninja-build g++
Depends: qt6-base-dev, qt6-tools-dev, qt6-l10n-tools, unzip, zlib1g-dev, libgl1-mesa-dri, mesa-common-dev, qt6-imageformats-plugins, libqt6svg6${DJVU_DEP}${LUA_DEP}${LIBRSVG_DEP}
Suggests: qt6-style-kvantum
Description: High performance Document and Image viewer
EOF

mkdir -p "$OUT_DIR"
OUT_FILE="$OUT_DIR/${APP_NAME}_${APP_VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$PKG_DIR" "$OUT_FILE"

echo "Built: $OUT_FILE"
