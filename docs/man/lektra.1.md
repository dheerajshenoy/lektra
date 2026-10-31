% LEKTRA(1) lektra 0.8.1 | User Manual
% Dheeraj Vittal Shenoy
% October 2026

# NAME

**lektra** — a high-performance, keyboard-driven document viewer that prioritizes screen space and control.

# SYNOPSIS

```
lektra [OPTIONS] [FILE_PATH(s)...]
```

# DESCRIPTION

**lektra** is a fast, keyboard-driven document viewer. By default, it detaches from
the terminal immediately after, so your shell prompt returns right away.
Use `--foreground` to suppress this and keep lektra attached to the terminal
— useful for debugging or capturing log output.

Supported file formats: PDF, XPS, OpenXPS, CBZ (Comic Book Zip), FictionBook 2.0 (FBZ), EPUB, MOBI, PNG, JPEG, TIFF, SVG, DjVu (only if compiled with djvu support)

Multiple files may be opened at once, optionally arranged in a split-view
layout.

Command line options are described below:

`-h`, `--help`
: Print a short usage summary and exit.

`-v`, `--version`
: Print the version string and exit.

`FILE_PATH(s)`
: One or more documents to open on startup.

`--list-commands`
: Print a list of all available commands and exit.

`-p PAGE_NUMBER`, `--page PAGE_NUMBER`
: Jump to the given page number immediately after opening the file.
  Defaults to `-1` (no override; reopen at last remembered position).

`--layout LAYOUT`
: Set the initial view layout. Valid values:

| Value        | Description                        |
|--------------|------------------------------------|
| `single`     | One document pane                  |
| `vertical`   | Two panes side-by-side (default)   |
| `horizontal` | Two panes stacked top-to-bottom    |
| `book`       | Two-page book spread               |
| `grid`       | Pages arranged in a grid           |

`--vsplit`
: Open the supplied file(s) in a **vertical** split view.

`--hsplit`
: Open the supplied file(s) in a **horizontal** split view.

`-s SESSION_NAME`, `--session SESSION_NAME`
: Load a named session on startup.
  See **lektra-config(5)** for how sessions are stored.

`-c CONFIG_PATH`, `--config CONFIG_PATH`
: Path to an alternative `config.toml` or `init.lua` file, selected by
  extension. Only that file is loaded: a `.toml` path loads just that
  configuration (compiled-in defaults otherwise, no `init.lua`), a `.lua`
  path loads just that script (no `config.toml`).
  See **lektra-config(5)** for the full configuration reference.

`--about`
: Show the *About Lektra* dialog and exit.

`--tutorial`
: Open with the built-in tutorial file loaded.

`-n`, `--new-window`
: Open the file(s) in a separate window, even if single-instance mode is on.

`--single-instance`
: Force single-instance mode for this invocation: if lektra is already
  running, the files are opened in that instance.

`--socket SOCKET_PATH`
: Listen on the given socket path instead of the default IPC socket.

`--foreground`
: Do **not** detach from the terminal. The process stays in the foreground,
  which is useful for capturing stdout/stderr or running under a debugger.

`--synctex-forward {pdf-file}#{src-file}:{line}:{col}`
: Perform a SyncTeX forward search, jumping to the location in the PDF that
  corresponds to the given source position.

    **Only available when lektra is compiled with synctex support.**

`--command`
: Specify one or more valid lektra commands (see `--list-commands`) chained together by ; (semicolon). The commands are executed left to right.

## STARTUP BEHAVIOUR

On launch, lektra performs a **double-fork** so that the grandchild process is
fully detached from the calling TTY and the parent shell returns immediately.
The re-executed grandchild always receives the implicit `--foreground` flag, so
the detach logic runs exactly once.

Pass `--foreground` explicitly to skip the fork entirely — useful when running
lektra from a compositor script, systemd user unit, or test harness.

## FILES

`$XDG_CONFIG_HOME/lektra/config.toml`
: Default user configuration file. See **lektra-config(5)**.

`$XDG_CONFIG_HOME/lektra/init.lua`
: Optional Lua script, run at startup for scripting and options.

`$XDG_DATA_HOME/lektra/sessions/`
: Directory where named sessions are persisted.

`$XDG_DATA_HOME/lektra/bookmarks.json`
: Saved bookmarks.

`share/lektra/lua/`, `share/lektra/lektra-config.schema.json`
: Installed Lua type stubs and the JSON Schema for `config.toml`, for
  completion in editors and language servers.

## EXAMPLES

Open a single file:

```
lektra document.pdf
```

Open at page 42:

```
lektra -p 42 document.pdf
```

Open two files side-by-side:

```
lektra --vsplit file1.pdf file2.pdf
```

Open with a custom config and a specific layout:

```
lektra --config ~/dotfiles/lektra.toml --layout book thesis.pdf
```

Run in the foreground (e.g. to see log output):

```
lektra --foreground document.pdf
```

Load a saved session:

```
lektra --session research
```

Open a file in a new window, even if lektra is already running:

```
lektra --new-window document.pdf
```

Open files in a grid layout:

```
lektra --layout grid page1.png page2.png page3.png page4.png
```

Use a Lua script as the only configuration:

```
lektra --config ~/dotfiles/init.lua document.pdf
```

Jump to a position in a PDF from a LaTeX source (SyncTeX):

```
lektra --synctex-forward "paper.pdf#paper.tex:120:1"
```

Run commands after opening a file

```
lektra --command "fullscreen; search hello; page_goto 4" document.pdf
```

## SEE ALSO

Configuration options and the Lua API are documented at <https://dheerajshenoy.github.io/lektra/configuration>

## BUGS

Please report bugs at: <https://github.com/dheerajshenoy/lektra/issues> or <https://codeberg.org/lektra/lektra/issues>.

## AUTHOR

Dheeraj Vittal Shenoy <dheerajshenoy22@gmail.com>
