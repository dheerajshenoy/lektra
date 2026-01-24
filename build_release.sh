#!/bin/sh
set -eu

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
OUT_DIR="$ROOT_DIR/dist"
APP_VERSION=${APP_VERSION:-$(sed -n 's/^[[:space:]]*project(lektra VERSION \([0-9.]*\).*/\1/p' "$ROOT_DIR/CMakeLists.txt" | head -n 1)}

if [ -z "$APP_VERSION" ]; then
    APP_VERSION="dev"
fi

mkdir -p "$OUT_DIR"

echo "Building release artifacts for version: $APP_VERSION"

# Build .deb
APP_VERSION="$APP_VERSION" "$ROOT_DIR/build_deb.sh"

# Build AppImage
APP_VERSION="$APP_VERSION" "$ROOT_DIR/build_appimage.sh"

# Move the newest AppImage into dist/
APPIMAGE_FILE=$(ls -t "$ROOT_DIR"/*.AppImage 2>/dev/null | head -n 1 || true)
if [ -z "$APPIMAGE_FILE" ]; then
    echo "Error: AppImage not found in repo root." >&2
    exit 1
fi

APPIMAGE_BASENAME=$(basename "$APPIMAGE_FILE")
mv -f "$APPIMAGE_FILE" "$OUT_DIR/$APPIMAGE_BASENAME"

echo "Artifacts:"
ls -1 "$OUT_DIR"
