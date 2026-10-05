# LEKTRA

Lektra is a document and image viewer built for performance, for as much screen
space for your documents as possible, and for configurability without
compromise. It works natively on Wayland (through Qt 6) and is scriptable with
Lua.

For screenshots and short demo videos see the [homepage](https://dheerajshenoy.github.io/lektra/).

## Features

* **Fast and Lightweight:** Opens instantly with a small memory footprint and smooth scrolling.
* **Handles Large Files:** High performance on massive documents by only rendering visible content at high zoom levels.
* **Versatile Viewing Modes:** Supports single page, continuous scrolling, book layout, presentation mode, and smart margin trimming.
* **Text Reflow:** Re-paginates EPUB, FB2, and Mobi files to fit your custom fonts, text size, and line spacing.
* **Comfort & Dark Mode:** Customize page colors, flip/rotate, invert colors, or use high-contrast themes for easy reading.
* **Rich Media Support:** Plays animated GIFs, WebPs, and APNGs, and displays multi-page TIFFs natively.
* **Outline & Thumbnails:** Fast visual navigation with side panels, including auto-generated outlines for unindexed PDFs.
* **Smart Search:** Search with regex, jump to hits above/below, and see search results marked directly on the scrollbar.
* **Keyboard Navigation:** Vim-like keybindings, link hints to click without a mouse, and jump history to move back and forward.
* **Bookmarks & Sessions:** Automatically reopens where you left off, saves custom workspaces, and allows bookmark import/export.
* **Tabs, Splits, & Portals:** Rearrange multi-document tabs, split windows, or open small linked view "portals" to read two parts of a file side-by-side.
* **Synchronized Views:** Link zoom, scroll, and rotation across multiple open documents for side-by-side comparison.
* **Smart Text & Image Capture:** Caret keyboard mode, double/triple click selection, and direct drag-and-drop image extraction to other apps.
* **Built-in Annotations:** Add highlights, notes, and rectangles directly to the PDF file, with inline commenting and annotation exporting.
* **Academic & SyncTeX Tools:** Jump seamlessly between LaTeX editor source code and PDF locations, plus view metadata and EXIF data.
* **Flexible Page Export:** Save page ranges or cropped regions as PNG, JPEG, WebP, PDF, SVG, Text, or HTML at any custom DPI.
* **Command Palette:** Quickly run frequency-sorted commands and find files using an Emacs-style file picker.
* **TOML & Lua Configuration:** Deeply customize keybindings, statusbars, and per-filetype options in simple TOML or Lua.
* **Scriptable Engine:** Powered by embedded LuaJIT with async background processing for custom commands, menus, and automated document tasks.
* **Experimental AI Assistant:** Optional integration with Ollama or OpenAI-compatible APIs to answer page questions or control the app.
* **Multilingual:** Available in English and Spanish.

## Supported file types

**Documents:** PDF, XPS, OXPS, CBZ (also CBR, CB7 and CBT), FB2, EPUB, Mobi and
DjVu (if `djvulibre` is installed on the system).

**Images:** JPG, PNG, APNG, BMP, WEBP, GIF, TIFF, ICO, SVG, PPM, PGM and PBM,
including animated images (GIF, WebP, APNG) and multi-page TIFFs. Edge cases of
SVG are handled by `librsvg` if it is installed.

## Where to go next

- [Installation](installation.md): packages for your system, or build from source
- [Configuration](reference/configuration.md): every option, for `config.toml` and `lektra.opt`
- [Commands](reference/commands.md): everything the command palette can run
- [Lua API](reference/lua_api.md) and the [scripting guide](LUA-WIKI.md)
- [Examples](examples.md): an `init.lua` and some small plugins

To get to know Lektra quickly, run `lektra --tutorial`, or read the
[tutorial as a PDF](tutorial/tutorial.pdf).
