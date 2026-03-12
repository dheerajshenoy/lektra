#!/bin/sh

VERSION=$(grep -m1 'project(' CMakeLists.txt | grep -oP 'VERSION \K[0-9]+\.[0-9]+\.[0-9]+')
DESTDIR="$PWD/pkg" cmake --install build
tar -czvf lektra-${VERSION}-x86_64.tar.gz -C pkg .
