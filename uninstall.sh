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
printf "Are you sure you want to uninstall 'lektra' and all associated files? [y/N]: "
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
# Check if lektra is installed
# ---------------------------------------------------------
if ! command -v lektra >/dev/null 2>&1; then
    echo "lektra is not installed (not found in PATH)."
else
    echo "lektra binary found in PATH: $(command -v lektra)"
fi

# ---------------------------------------------------------
# File list (POSIX array = space-separated variable)
# ---------------------------------------------------------
FILES="
bin/lektra
share/applications/lektra.desktop
share/icons/hicolor/16x16/apps/lektra.png
share/icons/hicolor/32x32/apps/lektra.png
share/icons/hicolor/48x48/apps/lektra.png
share/icons/hicolor/64x64/apps/lektra.png
share/icons/hicolor/128x128/apps/lektra.png
share/icons/hicolor/256x256/apps/lektra.png
share/icons/hicolor/512x512/apps/lektra.png
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
