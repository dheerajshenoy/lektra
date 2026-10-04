# Contributing

Thanks for your interest in contributing. You can contribute in many ways, including:

- Reporting bugs
- Suggesting features
- Help in UI language translation
- Documentation
- Man pages

This project is actively developed and maintained.
Please read this document carefully before opening an issue or pull request.

---

## Ground rules

- Be concise and technical.
- No drive-by PRs without understanding the codebase.
- If you are unsure about a change, open an issue first.
- Maintain existing style and architecture. Do not refactor unrelated code.

---

## Reporting bugs

Before opening an issue:

1. Make sure the issue is reproducible on the latest `main` branch.
2. Search existing issues to avoid duplicates.

When reporting a bug, include:

- Exact steps to reproduce
- Expected behavior
- Actual behavior
- Platform (OS, compiler, library versions)
- Relevant logs or screenshots if applicable

## Development setup

Clone the repository. MuPDF and LuaJIT are bundled as git submodules (LuaJIT
is built and linked statically, so Lua scripting needs no system Lua):

```bash
git clone --recurse-submodules https://codeberg.org/lektra/lektra.git
cd lektra
# already cloned without it?  git submodule update --init --recursive

mkdir -p build
cmake -S . -B build -DCMAKE_INSTALL_TYPE=Debug
cmake --build build --parallel
cmake --install build --prefix build/debug
```

### Windows

LuaJIT is built with its own `msvcbuild.bat` as part of the normal build, so
the Visual Studio C++ tools must be installed. CMake finds the matching
`vcvarsall.bat` itself, so building from an IDE or a plain prompt works; if
it cannot, run the build from a "Developer Command Prompt" or configure with
`-DWITH_LUA=OFF` (`configure.bat --without-lua`).

### Nix

A flake is provided that handles all dependencies including mupdf:

```bash
nix develop
nix build
```

### macOS: `Qt6::qtpaths references ... but this file does not exist`

If `find_package(Qt6 ...)` fails with this error against a Homebrew-installed
`qt`, it's a known Homebrew packaging quirk, not a Lektra issue: the Qt
CMake config looks for an unversioned `qtpaths` binary, but Homebrew's `qt`
formula only ships the versioned `qtpaths6` — the unversioned symlink is
supposed to exist too, but goes missing after some brew upgrade/relink
sequences. Fix it with:

```bash
ln -sf qtpaths6 "$(brew --prefix qt)/bin/qtpaths"
```

(`build_dmg.sh` detects and fixes this automatically before configuring.)
