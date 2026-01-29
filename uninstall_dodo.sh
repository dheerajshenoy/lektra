#!/bin/sh

set -e

printf "Starting uninstallation...\n"

# ---------------------------------------------------------
# Ask for prefix
# ---------------------------------------------------------
printf "Enter the install prefix used during installation (default: /usr/local): "
read PREFIX
PREFIX=${PREFIX:-/usr/local}

# ---------------------------------------------------------
# Confirm uninstall
# ---------------------------------------------------------
printf "Are you sure you want to uninstall 'dodo' and all associated files? [y/N]: "
read confirm

# default to "n"
[ -z "$confirm" ] && confirm="n"

# lowercase conversion (POSIX way)
confirm=$(printf "%s" "$confirm" | tr 'A-Z' 'a-z')

if [ "$confirm" != "y" ]; then
    echo "Cancelling uninstall."
    exit 0
fi

# ---------------------------------------------------------
# Check if dodo is installed
# ---------------------------------------------------------
if ! command -v dodo >/dev/null 2>&1; then
    echo "dodo is not installed (not found in PATH)."
else
    echo "dodo binary found in PATH: $(command -v dodo)"
fi

# ---------------------------------------------------------
# File list (POSIX array = space-separated variable)
# ---------------------------------------------------------
FILES="
bin/dodo
share/applications/dodo.desktop
share/icons/hicolor/16x16/apps/dodo.png
share/icons/hicolor/32x32/apps/dodo.png
share/icons/hicolor/48x48/apps/dodo.png
share/icons/hicolor/64x64/apps/dodo.png
share/icons/hicolor/128x128/apps/dodo.png
share/icons/hicolor/256x256/apps/dodo.png
share/icons/hicolor/512x512/apps/dodo.png
"

# ---------------------------------------------------------
# Remove files
# ---------------------------------------------------------
for file in $FILES; do
    TARGET="$PREFIX/$file"
    if [ -e "$TARGET" ]; then
        echo "Removing $TARGET"
        rm -rf "$TARGET"
    else
        echo "Skipping missing: $TARGET"
    fi
done

# ---------------------------------------------------------
# Done
# ---------------------------------------------------------
echo "Uninstallation completed."
