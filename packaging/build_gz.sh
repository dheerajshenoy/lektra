#!/bin/sh
set -e

# Everything below is relative to the repository root.
cd "$(dirname "$0")/.."

rm -rf build/
rm -rf pkg/
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DWITH_SYNCTEX=on -DWITH_LUA=on -DWITH_LLM_SUPPORT=on -G Ninja
ninja
cd ..
VERSION=$(grep -m1 'project(' CMakeLists.txt | grep -oP 'VERSION \K[0-9]+\.[0-9]+\.[0-9]+')
echo "Building GZ file for Lektra version: $VERSION"
DESTDIR="$PWD/pkg" cmake --install build
tar -czvf lektra-${VERSION}-x86_64.tar.gz -C pkg .
