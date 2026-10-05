# About

## What is Lektra?

Lektra is designed for high performance, for giving your documents as much
screen space as possible, and for configurability without sacrifice. It has good
Wayland support (thanks to Qt 6). Features like jump markers, history
navigation and sessions are things its author could not find in other PDF
readers.

If you are looking for a lightweight yet powerful reader that you can tailor to
your preferences, particularly if you read PDFs heavily for research, academics
or daily work, Lektra is a good choice.

## Libraries used

- [MuPDF](https://mupdf.com): documents and rendering
- [Qt 6](https://www.qt.io/development/qt-framework/qt6): the graphical user interface
- [LuaJIT](https://luajit.org): the embedded scripting language, bundled
- [TOML++](https://marzer.github.io/tomlplusplus): reading the config file
- [JSON for Modern C++](https://github.com/nlohmann/json): session files and recent files
- [Argparse](https://github.com/p-ranav/argparse): command line arguments
- [MicroTeX](https://github.com/NanoMichael/MicroTeX): LaTeX math in the LLM chat
- [SyncTeX](https://github.com/jlaurens/synctex): forward and inverse search
- [DjVuLibre](https://djvu.sourceforge.net/): DjVu files, optional
- [librsvg](https://wiki.gnome.org/Projects/LibRsvg), [libexif](https://libexif.github.io/) and [libarchive](https://www.libarchive.org/): optional

## How do I contribute?

Contributions are welcome. The source code is hosted on
[Codeberg](https://codeberg.org/lektra/lektra) and
[GitHub](https://github.com/dheerajshenoy/lektra), kept in sync as mirrors of
each other. Hosting on two platforms is a deliberate choice: if one becomes
unavailable, the project stays accessible and development can continue. Open
pull requests on either platform, and read
[CONTRIBUTING.md](https://codeberg.org/lektra/lektra/src/branch/main/CONTRIBUTING.md) first.

## How do I report bugs or request features?

Through the
[GitHub issues page](https://github.com/dheerajshenoy/lektra/issues).

## Thanks

To everyone who helped and helps make Lektra better (in no particular order):

- [barrettruth](https://github.com/barrettruth): the NixOS flake and issue report templates
- [techmanwalker](https://github.com/techmanwalker): initiating translations and the Spanish translation
- [linwaytin](https://codeberg.org/linwaytin): feedback, bug reports and the annotation comment feature request
- [douglarek](https://github.com/douglarek): packaging Lektra for [gentoo-zh](https://github.com/microcai/gentoo-zh)
- [Zou Yonghe](https://codeberg.org/budingZou): the macOS app bundle
- [fraterlinux](https://github.com/fraterlinux): testing and bug reports

If you want to support the project: [GitHub Sponsors](https://github.com/sponsors/dheerajshenoy)
or [Liberapay](https://liberapay.com/dheerajshenoy).
