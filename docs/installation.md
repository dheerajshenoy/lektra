# Installation

## Packages

=== "Arch Linux"

    Lektra is in the [Arch User Repository](https://aur.archlinux.org/packages/lektra-git).

    The latest stable release:

    ```sh
    paru -S lektra-bin    # or: yay -S lektra-bin
    ```

    The latest `main` branch:

    ```sh
    paru -S lektra-git    # or: yay -S lektra-git
    ```

=== "Ubuntu / Debian"

    Download the latest DEB package from the releases page on
    [Codeberg](https://codeberg.org/lektra/lektra/releases) or
    [GitHub](https://github.com/dheerajshenoy/lektra/releases) and install it.

    !!! note
        Tested on Ubuntu 24.04.

=== "AppImage"

    Download the latest AppImage from the releases page on
    [Codeberg](https://codeberg.org/lektra/lektra/releases) or
    [GitHub](https://github.com/dheerajshenoy/lektra/releases), make it
    executable and run it.

=== "Gentoo"

    ```sh
    eselect repository enable gentoo-zh
    emaint sync -r gentoo-zh
    emerge lektra
    ```

    !!! note
        This package is maintained by [douglarek](https://github.com/douglarek),
        not by the upstream project. Please report Gentoo-specific issues to the
        [gentoo-zh](https://github.com/microcai/gentoo-zh) repository.

=== "macOS"

    Requires Xcode or the Xcode Command Line Tools. Install the dependencies
    and build the DMG:

    ```sh
    brew install cmake ninja pkg-config qt
    git clone --recurse-submodules https://github.com/dheerajshenoy/lektra.git
    cd lektra
    ./build_dmg.sh
    ```

    This creates a DMG file in the project directory. Mount it and verify the
    bundled `lektra.app`:

    ```sh
    codesign --verify --deep --strict build-macos/lektra.app
    hdiutil verify dist/*.dmg
    ```

    !!! note
        Tested on Apple Silicon.

=== "Windows"

    Download the latest EXE from the releases page on
    [Codeberg](https://codeberg.org/lektra/lektra/releases) or
    [GitHub](https://github.com/dheerajshenoy/lektra/releases).

    !!! note
        Tested on Windows 11.

## Build from source

Building from source is the best way to get the latest features and bug fixes,
and to contribute to the project. The repository is on both
[Codeberg](https://codeberg.org/lektra/lektra) and
[GitHub](https://github.com/dheerajshenoy/lektra), kept in sync. The clone
commands below use GitHub; for Codeberg use `https://codeberg.org/lektra/lektra.git`.

Requirements:

- git
- Qt 6
- CMake 3.22 or newer
- C++20 compiler.

### Linux and macOS

Install the dependencies for your distribution:

=== "Arch Linux"

    ```sh
    pacman -S base-devel qt6-base cmake pkgconf
    ```

=== "Debian / Ubuntu"

    ```sh
    apt install build-essential pkgconf qt6-base-dev qt6-tools-dev \
        qt6-tools-dev-tools qt6-l10n-tools unzip zlib1g-dev cmake \
        libgl1-mesa-dri mesa-common-dev g++
    ```

=== "Other distributions"

    Look for the equivalent packages in your package manager.

Then build and install:

```sh
git clone --recurse-submodules https://github.com/dheerajshenoy/lektra.git
cd lektra
./configure    # ./configure --help lists the options; the prefix defaults to /usr
./install
```

!!! note
    You might need to run `./install` with `sudo` to install to system
    directories.

#### Optional dependencies

These are found at build time or loaded at run time when they are present:

- `djvulibre`: DjVu files
- `librsvg`: more accurate rendering of SVG images
- `libexif`: EXIF metadata in the properties of images
- `libarchive`: RAR and 7z comic books (CBR, CB7), at build time
- `fontconfig`: font lookup, at build time

### Windows

1. Install Qt 6 with the
   [Qt installer](https://doc.qt.io/qt-6/get-and-install-qt.html): the latest
   Qt 6 version with MSVC support, and add the Qt 6 `bin` folder to the `Path`
   environment variable.
2. Install [Visual Studio Community](https://visualstudio.microsoft.com/vs/community/)
   and add the C++ MSVC development tools with the Visual Studio Installer.
3. Install [CMake](https://cmake.org/download/).
4. Build:

    ```sh
    git clone --recurse-submodules https://github.com/dheerajshenoy/lektra.git
    cd lektra
    ./configure.bat
    ```

## Getting started

After installing, start Lektra from the application menu or with `lektra` in a
terminal.

`lektra --help` lists the command line options.

On Linux/macOS you can read the manual page using `man lektra`.

LEKTRA ships with a tutorial document that can be opened using `lektra --tutorial` from the command line, or use the
`show_tutorial_file` command from the command palette once inside LEKTRA.
