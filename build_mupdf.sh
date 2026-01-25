#!/bin/sh

set -eu


PREFIX="/usr/local"
BUILD_TYPE="Release"          # Release | Debug
EXTERN_DIR="external"
ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build"
JOBS=$(nproc)

# MuPDF source tarball
MUPDF_VERSION="1.27.0"
MUPDF_URL="https://mupdf.com/downloads/archive/mupdf-${MUPDF_VERSION}-source.tar.gz"
MUPDF_TARBALL="$EXTERN_DIR/mupdf-${MUPDF_VERSION}-source.tar.gz"
MUPDF_SRC_DIR="$EXTERN_DIR/mupdf-${MUPDF_VERSION}-source"
MUPDF_BUILD_DIR="$EXTERN_DIR/mupdf"   # normalized path (symlink/dir)

die() { echo "Error: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

download_mupdf() {
    mkdir -p "$EXTERN_DIR"
    if [ -f "$MUPDF_TARBALL" ]; then
        echo "MuPDF tarball already exists: $MUPDF_TARBALL"
        return 0
    fi
    echo "Downloading MuPDF $MUPDF_VERSION..."
    if have curl; then
        curl -L --fail -o "$MUPDF_TARBALL" "$MUPDF_URL" || die "curl download failed"
    elif have wget; then
        wget -O "$MUPDF_TARBALL" "$MUPDF_URL" || die "wget download failed"
    else
        die "Need curl or wget to download MuPDF"
    fi
}

extract_mupdf() {
    echo "Extracting MuPDF..."
    [ -f "$MUPDF_TARBALL" ] || die "MuPDF tarball missing: $MUPDF_TARBALL"
    rm -rf "$MUPDF_SRC_DIR" "$MUPDF_BUILD_DIR"
    tar -xf "$MUPDF_TARBALL" -C "$EXTERN_DIR" || die "tar failed"
    [ -d "$MUPDF_SRC_DIR" ] || die "Expected extracted dir not found: $MUPDF_SRC_DIR"
    mv "$MUPDF_SRC_DIR" "$MUPDF_BUILD_DIR"
}

build_mupdf() {
    if [ "$BUILD_TYPE" = "Debug" ]; then
        MUPDF_BUILD="debug"
    else
        MUPDF_BUILD="release"
    fi

    echo "Building MuPDF ($MUPDF_BUILD)..."
    [ -d "$MUPDF_BUILD_DIR" ] || die "MuPDF dir missing: $MUPDF_BUILD_DIR"

  # Quick workaround if your system lacks lcms2mt.h:
  # LCMS_FLAG="HAVE_LCMS=no"   # disables ICC color management
  LCMS_FLAG=""

  (cd "$MUPDF_BUILD_DIR" && \
      make -j"$JOBS" build="$MUPDF_BUILD" HAVE_X11=no HAVE_GLUT=no $LCMS_FLAG)

  # Validate expected libs exist (MuPDF’s build layout is stable here)
  [ -f "$MUPDF_BUILD_DIR/build/$MUPDF_BUILD/libmupdf.a" ] || die "MuPDF lib missing"
  [ -f "$MUPDF_BUILD_DIR/build/$MUPDF_BUILD/libmupdf-third.a" ] || die "MuPDF third lib missing"
}

mupdf_built() {
    [ -f "$MUPDF_BUILD_DIR/build/$MUPDF_BUILD/libmupdf.a" ] && \
    [ -f "$MUPDF_BUILD_DIR/build/$MUPDF_BUILD/libmupdf-third.a" ]
}

download_mupdf
extract_mupdf
build_mupdf
