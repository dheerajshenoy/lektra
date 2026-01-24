#!/bin/sh
set -eu

# --------------------------
# Defaults
# --------------------------
PREFIX="/usr/local"
BUILD_TYPE="Release"          # Release | Debug
ENABLE_LLM_SUPPORT="OFF"      # ON | OFF
DO_INSTALL=1                  # always copy into PREFIX (may sudo)
JOBS=""
CMAKE_EXTRA_ARGS=""
FORCE_RECONFIGURE=0
FORCE_REBUILD_MUPDF=0

# MuPDF source tarball
MUPDF_VERSION="1.27.0"
MUPDF_URL="https://mupdf.com/downloads/archive/mupdf-${MUPDF_VERSION}-source.tar.gz"

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
EXTERN_DIR="$ROOT_DIR/external"
MUPDF_TARBALL="$EXTERN_DIR/mupdf-${MUPDF_VERSION}-source.tar.gz"
MUPDF_SRC_DIR="$EXTERN_DIR/mupdf-${MUPDF_VERSION}-source"
MUPDF_BUILD_DIR="$EXTERN_DIR/mupdf"   # normalized path (symlink/dir)

STAGE_DIR="$ROOT_DIR/_stage"
BUILD_DIR="$ROOT_DIR/build"

die() { echo "Error: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

usage() {
    cat <<EOF
    Usage: ./install.sh [options]

Options:
--prefix <path>            Install prefix (default: /usr/local)
--release                  Build type Release (default)
--debug                    Build type Debug
--with-llm                 Enable LLM support
--without-llm              Disable LLM support (default)
-j <N>                     Parallel jobs (default: auto)
--cmake-arg "<arg>"        Extra CMake arg (repeatable)
--reconfigure              Delete build dir and reconfigure from scratch
--rebuild-mupdf             Force re-download/extract/build MuPDF
-h, --help                 Show help

Examples:
./install.sh
./install.sh --debug --with-llm -j 12
./install.sh --prefix \$HOME/.local
EOF
}

# --------------------------
# Parse args
# --------------------------
while [ $# -gt 0 ]; do
case "$1" in
    -h|--help) usage; exit 0 ;;
    --prefix)
    shift || die "--prefix requires a value"
    [ $# -gt 0 ] || die "--prefix requires a value"
    PREFIX="$1"
    ;;
    --release) BUILD_TYPE="Release" ;;
    --debug) BUILD_TYPE="Debug" ;;
    --with-llm) ENABLE_LLM_SUPPORT="ON" ;;
    --without-llm) ENABLE_LLM_SUPPORT="OFF" ;;
    -j)
    shift || die "-j requires a value"
    [ $# -gt 0 ] || die "-j requires a value"
    JOBS="$1"
    ;;
    --cmake-arg)
    shift || die "--cmake-arg requires a value"
    [ $# -gt 0 ] || die "--cmake-arg requires a value"
    CMAKE_EXTRA_ARGS="$CMAKE_EXTRA_ARGS $1"
    ;;
    --reconfigure) FORCE_RECONFIGURE=1 ;;
    --rebuild-mupdf) FORCE_REBUILD_MUPDF=1 ;;
    *) die "Unknown argument: $1 (use --help)" ;;
esac
shift
done

# --------------------------
# Jobs auto
# --------------------------
if [ -z "$JOBS" ]; then
    if have getconf; then
        JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
    else
        JOBS=1
    fi
fi

have cmake || die "cmake not found"
have make || die "make not found"

# Ninja optional
if have ninja; then
    GEN="-G Ninja"
    BUILD_CMD="ninja"
    INSTALL_CMD="ninja install"
else
    GEN=""
    BUILD_CMD="cmake --build . -- -j$JOBS"
    INSTALL_CMD="cmake --install ."
fi

# Map CMake -> MuPDF build name
if [ "$BUILD_TYPE" = "Debug" ]; then
    MUPDF_BUILD="debug"
else
    MUPDF_BUILD="release"
fi

echo "Lektra installer"
echo "  prefix:      $PREFIX"
echo "  build type:  $BUILD_TYPE"
echo "  llm:         $ENABLE_LLM_SUPPORT"
echo

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

configure_build_lektra() {
    echo "Building lektra..."
    if [ "$FORCE_RECONFIGURE" -eq 1 ]; then
        rm -rf "$BUILD_DIR"
    fi
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

  # shellcheck disable=SC2086
  cmake .. $GEN \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DENABLE_LLM_SUPPORT="$ENABLE_LLM_SUPPORT" \
  $CMAKE_EXTRA_ARGS

  sh -c "$BUILD_CMD"

  rm -rf "$STAGE_DIR"
  DESTDIR="$STAGE_DIR" sh -c "$INSTALL_CMD"
  cd "$ROOT_DIR"
}

final_install() {
    echo "Staged install created at:"
    echo "  $STAGE_DIR$PREFIX"
    echo

  SRC="$STAGE_DIR$PREFIX"
  [ -d "$SRC" ] || die "Staged prefix missing: $SRC"

  echo "Installing into $PREFIX ..."
  if [ -w "$PREFIX" ] 2>/dev/null; then
      mkdir -p "$PREFIX"
      cp -a "$SRC"/. "$PREFIX"/
  else
      echo "Prefix not writable; using sudo."
      sudo mkdir -p "$PREFIX"
      sudo cp -a "$SRC"/. "$PREFIX"/
  fi
  echo "Install complete."

  # Cleanup build dirs after install.
  rm -rf "$BUILD_DIR" "$MUPDF_BUILD_DIR/build"

  # Remove staging dir after a successful install.
  rm -rf "$STAGE_DIR"
}

# --------------------------
# Workflow
# --------------------------
if [ "$FORCE_REBUILD_MUPDF" -eq 0 ] && mupdf_built; then
    echo "[1/3] MuPDF (already built, skipping)"
else
    echo "[1/3] MuPDF"
    download_mupdf
    extract_mupdf
    build_mupdf
fi

echo "[2/3] lektra"
configure_build_lektra

echo "[3/3] stage/install"
final_install
