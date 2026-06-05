# LEKTRA CHANGELOG

## 0.7.4

### New Features

- Add **narrow to region** (Emacs-style). Invoke `narrow_to_region` (or *View → Narrow to
  Region*) to enter rubber-band selection; the chosen rectangle becomes the entire viewport —
  scrolling is constrained to it, everything outside is painted over with the background
  colour, and all interactions (text selection, zoom, search, links) work normally within the
  region. `wide_region` (or *View → Widen*) restores the full document view. The narrow state
  survives zoom changes: the region is stored in normalised page-local coordinates and
  recomputed from the page item's current transform after each re-render. The narrow region is
  also accessible from the region-selection context menu ("Narrow to Region").
- Expose `view:narrow_to_region()`, `view:wide_region()`, and `view:is_narrowed() -> boolean`
  in the Lua view API.

- Add horizontal and vertical page flip (`flip_horizontal` / `flip_vertical` commands, default
  bindings `|` / `_`). Flip state is stored in `Model` alongside rotation and propagated
  through every coordinate-space transform (`buildPageToDevMatrix` / `buildRenderTransform`
  helpers) so that text selection, link hit-testing, annotation picking, and all other
  page-space operations remain correct when a document is flipped. All render backends are
  supported: MuPDF PDF/XPS/CBZ, static images, animated GIFs, and DjVu. Commands are
  exposed as `view:flip_horizontal()` and `view:flip_vertical()` in the Lua view API.
- Add `view:region_select(callback)` Lua API. Switches the view into rubber-band
  selection mode; when the user draws a rectangle the callback receives
  `{ x, y, w, h }` in scene coordinates and the view returns to normal. The default
  context menu is bypassed so scripts can use the region for arbitrary purposes.
- Add "Copy Region as Image (Custom DPI)..." to the region-selection context menu.
  For PDF and other vector sources the selected region is re-rendered from the cached
  MuPDF display list at the requested DPI (72–1200, default 300), so the clipboard
  image is sharp regardless of the current view zoom. Only the selected sub-region is
  rasterized — not the whole page. Rotation and flip state are preserved in the new
  render transform. For raster sources (images, DjVu) the existing crop is upscaled
  using smooth transformation to approximate the requested resolution.
- Add `lektra.timer` Lua module backed by `QTimer`. Timers are created with
  `lektra.timer.new(interval_ms, callback [, single_shot])`, support `start`, `stop`,
  `set_interval`, `set_single_shot`, `is_active`, `is_single_shot`, `interval`, and
  `destroy`. Timers are parented to the main window so they are cleaned up automatically
  on shutdown; the `__gc` metamethod ensures the `QTimer` and Lua callback reference are
  released as soon as the userdata is garbage-collected, making explicit `destroy()` calls
  optional rather than required.
- Add `view:rotate_clock()`, `view:rotate_anticlock()`, `view:flip_horizontal()`, and
  `view:flip_vertical()` to the Lua view API (`lektra.view` methods on `View` userdata).
  Rotation methods were previously missing from the view API entirely.
- Implement SyncTeX forward search (editor → lektra). `--synctex-forward` now calls
  `synctex_display_query` and jumps to the matching PDF location via `GotoLocation`,
  including deferred-render support. Previously the flag parsed the arguments but never
  performed the jump (was a TODO).
- Add `--socket <path>` CLI flag. Starts the IPC server on the given socket path,
  allowing multiple lektra instances to be addressed independently (analogous to
  `nvim --listen`). The first invocation with a given socket listens; subsequent
  invocations with the same socket forward their message and exit.
- Add `--single-instance` CLI flag. Forces single-instance mode for one invocation
  without requiring `behavior.single_instance = true` in the config file.
- `--synctex-forward` now implicitly acts as single-instance: it always attempts to
  connect to a running instance via IPC regardless of the `single_instance` config
  setting, preventing duplicate windows when triggered from an editor.
- SyncTeX forward search via IPC now reuses an already-open tab for the target PDF
  instead of opening a new one each time. The matching tab is brought to focus and
  the view jumps to the synctex position in-place.
- Add focus border for the active split pane. Three new `split` config options:
  `split.focus_border` (bool, default `false`) enables the feature;
  `split.focus_border_color` (ARGB integer, default `0xFF4FC3F7`) sets the border colour;
  `split.focus_border_width` (integer, default `2`) sets the thickness in pixels. All three
  are exposed in the Lua opt API as `lektra.opt.split.focus_border`,
  `lektra.opt.split.focus_border_color`, and `lektra.opt.split.focus_border_width`.

- Add `portal.split` config option (`"vertical"`, `"horizontal"`, or `"smart"`).
  Previously portals always opened in a vertical split. `"smart"` automatically picks
  vertical when the view is wider than tall and horizontal otherwise.
- Add `FilePicker` — an Emacs-style find-file picker (`file_picker` command, default
  binding `Ctrl+Shift+o`). The prompt label shows the current directory (abbreviated
  with `~`); the search box filters entries in that directory. Typing a path with a `/`
  separator auto-navigates to the directory part. Tab completes the best match:
  directories are entered immediately, files are completed in the input. Backspace/Delete
  on an empty input navigates up one directory.

### Bug Fixes

- Fix background colour being overridden by the system palette colour on scroll
  or zoom. `initGui` was setting the background brush only on `m_gscene`
  (`QGraphicsScene`), whose `drawBackground()` only fills the scene rect.
  Viewport areas that fall outside the scene rect (visible when zoomed out or
  near document edges) were painted with the system palette window colour
  instead of the configured background. The brush is now set on both `m_gview`
  (`QGraphicsView`) — which fills the entire viewport — and `m_gscene` so that
  the narrow-clip strip painting in `GraphicsView::paintEvent` continues to read
  the correct colour from `scene()->backgroundBrush()`.

- Fix thumbnail panel defaulting to single-page layout instead of vertical. `handleOpenFileFinished` unconditionally called `setLayoutMode(m_config.layout.mode)` on every file open, overwriting the vertical layout set during thumbnail view construction. The call is now skipped when in thumbnail mode.
- Fix text-selection quads persisting on screen after navigating to a different page via the thumbnail panel. `GotoPage` now calls `ClearTextSelection()` before rendering in single-page layout mode, where the entire page is replaced.
- Fix thumbnail page highlight disappearing after the page item is re-rendered (e.g. after a zoom change). `renderPageFromImage` now saves the `isHighlighted()` state of the old item before deleting it and restores it on the newly created item.
- Fix thumbnail page highlight disappearing when a highlighted page scrolls off-screen and its item is deleted. The highlight state is now re-applied in `renderPageFromImage` whenever an existing highlighted item is replaced, covering both the re-render and the scroll-back-into-view paths.
- Fix `GoForwardHistory` always returning immediately due to a malformed guard condition. The expression `m_loc_history_index + static_cast<int>(m_loc_history.size())` (a large positive sum, always truthy) was missing a comparison operator; corrected to `m_loc_history_index + 1 >= static_cast<int>(m_loc_history.size())`.

- Add `behavior.cache_password` config option (default `true`). When
  auto-reloading a password-protected document, the password entered at open
  time is reused automatically. Set to `false` to prevent the password from
  persisting in memory beyond the initial unlock; auto-reload will then fail
  with an explanatory message for encrypted files.
- Fix auto-reload from disk being unreliable. Three bugs: (1) the file-stability
  check compared two `QFileInfo` size readings taken back-to-back with no delay,
  so it always returned "stable" even while the file was still being written (e.g.
  latexmk truncates the PDF to 0 bytes before rewriting it); size is now compared
  across two 100 ms timer ticks. (2) `QFileSystemWatcher::fileChanged` can fire
  multiple times for a single atomic file replace, spawning concurrent reload
  chains that caused double reloads; a `m_reload_pending` guard now prevents this.
  (3) The watcher path was only re-added after a *successful* reload, so a
  transient corrupt file would permanently stop watching for future saves; the
  re-add is now unconditional.
- Fix `--single-instance` CLI flag not triggering IPC forwarding. The probe condition
  used `readSingleInstanceFromConfig()` (reads the TOML file) and ignored the in-memory
  flag set by `--single-instance`, so the flag only started a server but never forwarded
  to an existing instance. Both sources are now combined before the probe runs.
- Fix SyncTeX IPC tab reuse only searching the root view of each container, missing PDFs
  open in split panes. Now uses `getAllViews()` to search all views in the container.

- Fix window title showing `"Argument missing"` warning when `title_format` used `{}`
  placeholder in the default value (`"{} - lektra"`), which is incompatible with
  `QString::arg()`. Default changed to `"%1 - lektra"` to match the existing TOML-loading
  path that already performs the `{}` → `%1` substitution.
- Fix crash on exit caused by `Lektra::~Lektra()` calling `lua_close(m_L)` before
  `m_command_manager` was destroyed. Commands registered from Lua hold `LuaRefGuard`
  `shared_ptr`s whose destructor calls `luaL_unref` — which requires the Lua state to still
  be alive. `m_command_manager` is now explicitly reset before `lua_close` so those
  destructors fire in the correct order.

### Improvements

- Change cursor to a crosshair (`Qt::CrossCursor`) when in text-highlight mode, switching
  to the I-beam only while actively dragging a selection. The default arrow is restored on
  mode exit.

- Fix `BrowseLinkItem` hover highlight rendering as nearly-black instead of yellow due to
  `QColor` being constructed with float literals `(1.0, 1.0, 0.0)` that were implicitly
  truncated to integers `(1, 1, 0)`. Corrected to `(255, 255, 0, 125)`.
- Fix internal links targeting page 0 (the first page) being silently ignored. The guard
  `if (_pageno)` evaluated to false for page 0; corrected to `if (_pageno >= 0)`.
- Fix float-to-int truncation in `highlightAnnotColor` and `DeleteAnnotationsCommand::undo`
  where `static_cast<int>(x * 255)` could produce off-by-one values (e.g. 254 instead of
  255). Now uses `qRound()`.
- Remove duplicate non-const `Model::DPI()` overload that shadowed the canonical
  `[[nodiscard]] const` version and caused the `[[nodiscard]]` attribute to be bypassed on
  non-const `Model` objects.
- `supports_save()`, `supports_encryption()`, `supports_decryption()`, and `isImage()` in
  `Model` were not marked `const noexcept` despite being pure queries with no side effects.
- Z-value and zoom-limit constants in `DocumentView` were defined as preprocessor macros
  (`#define`); replaced with typed `static constexpr` values.
- `m_spacing` in `DocumentView` was declared `double` but initialized with a `float`
  literal (`10.0f`); corrected to `int`.
- `BrowseLinkItem::_uri` was a raw `char*` with no ownership contract, risking dangling
  pointer access when MuPDF frees the underlying string. Changed to `QString` with a
  `const QString &` setter.

### Bug Fixes (Model / DocumentView)

- Fix `highlightAnnotColor` in `Model` using `static_cast<int>(x * 255)` which could
  produce off-by-one color values; corrected to `qRound()`.
- Fix `removeAnnotComment` declared in `Model.hpp` but never implemented; added the
  missing definition in `Model.cpp`.
- Fix duplicate `"Animated"` entry being pushed twice into the properties list for image
  files in `Model::properties()`; removed the redundant line.
- Fix `get_obj_num_at_rect` calling `pdf_load_page` without any `fz_try`/`fz_catch`
  guard, which could crash on a malformed PDF or out-of-range page number. Wrapped in
  `fz_try`/`fz_always`/`fz_catch` with proper page cleanup.
- Fix `getFirstCharPos` using `return` inside an `fz_try` block (bypassing `fz_always`
  cleanup) and manually dropping `page` and `stext_page` before returning, causing a
  double-free when combined with the `fz_always` block. Replaced with a `found` flag that
  exits the nested loops normally so `fz_always` performs the single correct cleanup.
- Fix `ScrollDown_HalfPage` and `ScrollUp_HalfPage` using `m_page_items_hash[m_pageno]`
  which silently inserts a null entry and immediately dereferences it, causing a crash when
  the current page is not yet rendered. Changed to `.value(m_pageno, nullptr)` with a null
  guard.
- Fix `renderAnnotations` and `renderLinks` using `m_page_items_hash[pageno]` (inserting
  null on miss) instead of `.value(pageno, nullptr)`.
- Fix `annotColorChangeRequested` lambda in `renderAnnotations` querying
  `m_model->getAnnotColor(m_pageno, ...)` using the current page instead of the captured
  `pageno`, returning the wrong color for annotations on non-current pages.
- Fix `renderLinks` early-return guard using `&&` across all three conditions, meaning an
  unsupported-links model only skipped rendering when the other two conditions also held.
  Split into two independent guards.
- Fix `clearVisiblePages` removing scene items without deleting them, leaking every
  `GraphicsImageItem` on document close or reload. Added `delete item` before
  `removeItem`, consistent with `clearVisibleLinks` and `clearVisibleAnnotations`.
- Fix `ensureSearchItemForPage` returning a cached item only when text search is *not*
  supported — the condition was inverted. Corrected to `if (supports_text_search() && ...)`.
- Fix `Copy_page_image` calling `pageAtScenePos` with a widget-space `QPoint` from
  `viewport()->rect().center()` instead of a scene-space coordinate; the result was
  immediately overwritten by the correct call. Removed the dead first call.

### Bug Fixes (DocumentContainer)

- Fix `focusView()` skipping assignment of `m_current_view` when it was `nullptr` — the
  guard `if (m_current_view && m_current_view != view)` required a non-null current view,
  so the first focus call after construction never set `m_current_view` or emitted
  `currentViewChanged`. Corrected to `if (m_current_view != view)` with a separate null
  check before deactivating the old view.
- Fix `closeView()` emitting `viewClosed` before `m_current_view` was updated, so any slot
  responding to the signal would observe a stale (already deleted) current view. Moved the
  `emit viewClosed(view)` to after the `m_current_view` reassignment block in both branches.
- Fix `closeThumbnailView()` and `focusThumbnailView()` being empty stubs that only checked
  for a null `m_thumbnail_view` and returned. Replaced with inline implementations delegating
  to `closeView(m_thumbnail_view)` and `focusView(m_thumbnail_view)` respectively.
- Fix `createThumbnailView()` connecting the `viewClosed` lambda without
  `Qt::UniqueConnection`, causing the lambda to accumulate duplicate connections on repeated
  calls. Added `Qt::UniqueConnection` to the `connect` call.

### Performance / Code Quality (DocumentContainer)

- `thumbSize = totalSize * 0.15` in `equalizeStretch` silently truncated a `double` result
  to `int`; changed to `static_cast<int>(totalSize * 0.15)`.
- The QSplitter handle stylesheet string `"QSplitter::handle { background-color: palette(mid); }"`
  was duplicated across five call sites in `DocumentContainer.cpp`; extracted to a
  `static const char *const SPLITTER_STYLESHEET` at file scope.

### Performance / Code Quality (Model / DocumentView)

- `HSCROLL_STEP` and `VSCROLL_STEP` in `DocumentView.cpp` were preprocessor macros;
  replaced with `static constexpr int`.
- `switch ((int)m_model->rotation())` used a C-style cast; changed to `static_cast<int>`.
- `img.save(fileName, format.toStdString().c_str())` created a temporary `std::string`
  to obtain a `const char*`; changed to `format.toLatin1().constData()`.
- `PageDimensionCache::reset()` assigned integer `0` to a `vector<bool>`; corrected to
  `false`. C-style casts `(int)` in `set()`, `getOrDefault()`, and `get()` replaced with
  `static_cast<int>`.
- Redundant `reserve()` calls before copy-assignment of `links` and `annotations` vectors
  in `renderPageWithExtrasAsync` removed; copy-assign allocates its own storage.

### Performance / Code Quality

- `LRUCache::put` unconditionally called `remove(key)` before every insert, incurring a
  redundant map lookup for the common new-key path. Inlined the existence check to avoid
  the extra traversal.
- `trim_ws` in `utils.hpp` trimmed leading whitespace with a per-character `erase` loop
  (O(n²)); replaced with a single `erase(begin, find_if_not(...))` call.
- `GraphicsImageItem::height()`, `quad_y_center()`, and `charEqual()` were missing
  `noexcept` despite being trivially non-throwing; added for consistency with surrounding
  functions.
- `Show_highlight_search()` and `Show_annot_comment_search()` used `&&` instead of `||`
  in their null-guard (`!m_doc && !m_doc->model()->...`), causing a null pointer
  dereference when `m_doc` was null. Corrected to `||`.
- `Tab_goto` bounds check used `||` instead of `&&` (`index > 0 || index < count`),
  making the condition almost always true and allowing out-of-range indices to pass.
  Corrected to `index >= 1 && index <= count`.
- `ShowAbout` leaked an `AboutDialog` instance on every call since the dialog was
  heap-allocated but never freed. Added `WA_DeleteOnClose` so each dialog self-destructs
  when closed.
- `OpenFilesInNewTab` warning message claimed extra files would be processed with no
  callback, but the function returned immediately. Message updated to accurately state
  that the operation is aborted.
- `std::move` was called on a `const QStringList &` parameter in `OpenFilesInNewTab`,
  `OpenFilesInVSplit`, and `OpenFilesInHSplit`, silently falling back to a copy.
  Corrected to plain assignment; the lambda captures in VSplit/HSplit now move `qfiles`
  correctly.

- Fix `n`/`N` search navigation skipping hits on the current page and jumping directly to
  the next/previous page. `getClosestHitIndex` now steps by flat hit index when the current
  hit is on the visible page, falling back to page-level anchoring only when the user has
  scrolled to a different page.
- Scrollbars are kept visible while search hit markers are drawn on them
  (`scrollbars.search_hits = true`). The auto-hide timer and mouse-leave events no longer
  dismiss the scrollbar during an active search; normal auto-hide resumes once the search
  is cancelled or cleared.
- Fix jump marker rendering at the wrong position after a zoom change. The marker's
  location is now stored as a `PageLocation` (page + document-space coordinates) instead
  of a scene-space point, so `Reshow_jump_marker` recomputes the correct scene position
  at call time regardless of zoom level.

## 0.7.3

### Features

- Add **Comment** to the text selection context menu. Selecting text and choosing Comment
  now opens an input dialog, then creates a highlight annotation with the comment embedded
  as a single undoable operation — no need to first highlight and then right-click the
  annotation to add a comment.
- Add **Copy Text** to the highlight annotation context menu. Copies the exact highlighted
  text to the clipboard by testing each character's centre against the annotation's quad
  points, using the cached stext page so no extra parsing is needed.
- Ability to open multiple files using file dialog to open in new tab, vsplit or hsplit.
- `DocumentView` no longer inherits `QOpenGLWidget` — it is a plain `QWidget` that hosts
  the `GraphicsView`; all GPU work goes through the view's own `QOpenGLWidget` viewport.
- Touch events are now correctly re-applied to the new viewport after `applyBackend()`
  replaces it.
- A global `QSurfaceFormat` (depth 24, stencil 8) is set before `QApplication`
  construction to ensure a well-formed OpenGL context on all platforms.

#### Lua API

- New dispatch event
    - `OnShutdown` - Dispatched when the application is shutting down
- Event callbacks now receive typed arguments instead of a raw `Lektra` pointer for
  events where more specific data is available:
    - `OnScreenChanged` — callback receives a `ScreenInfo` table with fields:
      `name`, `dpr`, `logical_dpi`, `physical_dpi`, `refresh_rate`, and
      `geometry` (`{x, y, w, h}`)
    - `OnTabChanged`, `OnTabRemoved` — callback receives the tab index as an integer
- Lua stubs: `ScreenInfo` class added with full field annotations

### Bug Fixes

- **Save File** menu action is now enabled only when the document has unsaved changes,
  providing a clear visual signal of modified state. A new `modifiedChanged(bool)` signal
  on `DocumentView` drives the update so the menu reacts immediately on each edit.
- **File Properties** menu action is now enabled for all open file types, not just PDF.
- **Back / Forward** history navigation actions are now enabled only when there is actually
  somewhere to navigate: `canGoBack()` and `canGoForward()` methods were added to
  `DocumentView`, a `historyChanged()` signal is emitted from `addToHistory`,
  `GoBackHistory`, and `GoForwardHistory`, and a dedicated
  `updateHistoryNavigationActions()` keeps the menu items in sync on every history change
  and on tab switch.
- **Invert Colour** menu item checked state is now synced on tab switch. Previously
  switching between tabs with different invert states left the checkbox stale; it is now
  updated in `updateUiEnabledState`.
- Fix annotation comment edits (right-click annotation → Add Comment) not being tracked by
  the undo stack. Comments are now pushed as `AnnotCommentCommand` entries, so they can be
  undone/redone correctly and the modified indicator stays in sync.
- Fix save being available after a save → undo → redo cycle. The undo stack correctly
  returns to its clean index on redo, but MuPDF's internal mutation tracker still reported
  unsaved changes because it sees the undo and redo as two separate edits. `SaveFile` now
  uses `m_is_modified` (driven by the undo stack's clean state) rather than
  `pdf_has_unsaved_changes` to decide whether a save is needed.
- Fix zoom glitch in multi-page document mode: interactive zoom (pinch/scroll) now uses an
  O(1) GPU view-transform (`QGraphicsView::scale`) for each step, deferring the O(n)
  `repositionPages()` call until the scroll-debounce timer settles. This eliminates the
  jarring per-page resize flash that was visible during zoom.
- Fix multi-page text selection breaking when scrolling: pages within the active selection
  range are now protected from eviction by `removeUnusedPageItems`, preventing gaps in the
  rendered quads and `pageAtScenePos` failures on the anchor page.
- Fix fit mode not working properly for images/djvu files after rotating
- Fix pickers (command palette, outline, bookmarks, etc.) leaking key events and shortcuts
  to the focused `DocumentView` while open. Pickers now grab the keyboard on launch and
  install an application-level event filter to swallow `QShortcutEvent`s, both of which
  are released on dismiss or item acceptance.
- Fix `InputDialog` ok and cancel buttons looking flat and weird.
- Render highlight annotations spanning multiple lines correctly by splitting the annotation quad into separate quads
  for each line. Previously, single rectangular quad spanning the multi-line highlight was rendered.

## 0.7.2

### Features

- Add Lua API `view:export_highlights(path)` that serialises all highlight annotations to
  a JSON file. Each entry contains `page` (1-based), `text`, and optionally `comment`.
  Returns `true` on success or `nil, error` on failure.

### Bug Fixes

- Fix crash when right-clicking on the overlay scrollbar — right-click events were
  unconditionally forwarded to the scrollbar, causing its built-in context menu
  (`QMenu::exec`) to spin a nested event loop while `GraphicsView::contextMenuEvent`
  also fired, leading to a double-menu crash. Non-left-button clicks on the scrollbar
  are now silently ignored.
- Fix animated GIF frames being skipped: `QImageReader::read()` auto-advances the frame
  position for animated formats, so the subsequent `jumpToNextImage()` call was
  double-advancing and skipping every other frame.
- Fix animated GIF playback running at ~1.5× speed: the elapsed clock was started before
  the first timer fired, so the first callback measured ~100 ms of wait time as "render
  overhead" and scheduled the next frame at 0 ms delay. Subsequent frames alternated
  between 0 ms and ~95 ms, averaging half the intended interval. The clock is now
  restarted at the top of each callback so it measures only actual render overhead.
- Fix image files always rendering blurry: pixel dimensions were stored directly as
  typographic points in the page dimension cache, causing `pageSceneSize` and
  `repositionPages` to apply an extra ×(dpi/72) upscale on every render. Dimensions
  are now converted to pts (`px * 72 / dpi`) on load, matching the DjVu path.
- Fix image zoom leaving blurry pixels: `setZoomAnchored` for images only applied a
  Qt scene-transform scale and never triggered a pixel-level re-render. The HQ render
  timer is now started after each anchor zoom so that, once zoom settles, `renderImage`
  re-renders at the exact target dimensions using `Qt::SmoothTransformation`.
- Add `collectHighlightTexts()` on `Model` returning a `std::vector<HighlightText>` with
  `page`, `text`, `comment`, and `quad` fields. Multi-line highlights are grouped into a
  single entry (lines joined with a space) rather than one entry per line.

### Performance

- Animated GIF memory usage reduced from O(all frames) to O(1 frame): switched from
  pre-decoding every frame into a `QList<QImage>` buffer to `QMovie` with
  `CacheMode::CacheNone`, which decodes one frame at a time on demand. The manual
  timer and elapsed-clock machinery is replaced by `QMovie::frameChanged`.

### SVG Rendering

- SVG files are now rendered via librsvg + Cairo when available, with automatic fallback
  to Qt's `QSvgRenderer`. librsvg is probed at runtime using `QLibrary` (no build-time
  dependency, no configuration flag) — if `librsvg-2.so.2` and `libcairo.so.2` are
  installed on the system they are used automatically, otherwise rendering falls back
  silently. librsvg handles CSS class-based styles, `<switch>` fallback elements, and
  CSS functions such as `light-dark()` that Qt's renderer ignores, producing correct
  output for SVGs generated by tools like draw.io.

### DjVu

- DjVu support is now detected at runtime via `QLibrary` instead of requiring a
  compile-time `WITH_DJVU` flag and a link-time dependency on libdjvulibre.
  If it is installed on the system, DjVu files are opened automatically;
  if it is absent, DjVu is silently unavailable. The `WITH_DJVU` CMake option and the
  `HAS_DJVU` preprocessor define have been removed.

### Breaking Changes

- **Remove `ImageMagick` dependency as it's a headache to maintain and to link against for cross-platform compatibility.**
- Removed `lektra.capabilities` table as it's not useful

## 0.7.1

### Features

- Auto scroll on text selection mode to keep the selection in view when selecting text
  with the mouse or keyboard, which provides a smoother and more intuitive text selection experience,
  especially for longer documents where the selected text may go out of view.
- Add optional lua scripting support (experimental, work in progress)
    - API overview
        - `lektra.opt` - for getting and setting config options
        - `lektra.cmd` - for command related stuff
        - `lektra.ui`  - for UI related stuff (e.g. showing notifications, input dialogs, etc.)
        - `lektra.tabs` - for managing tabs
        - `lektra.event` - for subscribing to events (e.g. page change, file open, etc.)
        - `lektra.keymap` - for managing keybindings
        - `lektra.mousemap` - for managing mousebindings
        - `lektra.utils` - for utility functions
        - `lektra.version` - for version functions
        - `lektra.capabilities` - for querying about compiled options
        - `lektra.bookmarks` - for managing bookmarks

    - Check [LUA-WIKI.md](LUA-WIKI.md) for more details and examples of the lua scripting support in LEKTRA.

- Vim/Emacs like search hit indexing navigation if `absolute_jump = false` in `[search]`
- Add bookmarks support with a bookmark picker to view and manage bookmarks. Bookmarks allow users to save specific locations
  in the document for quick access later.
- New bookmark related commands:
    - `bookmark_add` to add a bookmark at the current location
    - `bookmark_remove` to remove a bookmark at the current location
    - `bookmarks` to open the bookmark picker with the list of bookmarks in the document
- Add image rotation support
- Add missing implementation for `single_instance` option. Now if `single_instance` is enabled, new files will use the
  existing instance of LEKTRA instead of opening a new instance, which allows for better management of
  multiple documents and prevents cluttering the taskbar with multiple instances of LEKTRA.

### Bug Fixes

- Fix crash when right-clicking on the overlay scrollbar — right-click events were unconditionally
  forwarded to the scrollbar, causing its built-in context menu (`QMenu::exec`) to spin a nested
  event loop while the GraphicsView's `contextMenuEvent` also fired, leading to a double-menu crash.
  Non-left-button clicks on the scrollbar are now silently ignored.
- Update the layout menu item names
- Fix crash (SEGV) on click selection in `SINGLE` layout mode — `pageAtScenePos` was
  guarding the `outPageItem` assignment with `if (outPageItem)`, but the pointer is always
  `nullptr` at that point, so `pageItem` was never set and `mapFromScene` dereferenced null.
- Fix wrong text selection after zoom in `SINGLE` layout mode — `setZoomAnchored` called
  `repositionPages()` (which scales the old item as a visual intermediate) but never
  triggered a re-render, leaving the page item permanently at a non-unity scale. Because
  `mapFromScene` divides by the item scale, selection coordinates were passed to
  `computeTextSelectionQuad` in the wrong zoom space. Fixed by calling `renderPage()` for
  `SINGLE` mode after `repositionPages()` in `setZoomAnchored`.
- Fix fit mode not working if image files are opened
- Fix memory leak in `extractText` function in `Model` class
- Add UTF-8 text conversion for file paths on Windows to fix issues with opening files with non-ASCII characters in their paths on Windows.
- Reset `m_success = false` at the start of every open in `Model.cpp`
- [DocumentView.cpp] Made the future watcher connection single-shot and cleared stale connections before reconnecting and
  added a guard in `handleOpenFileFinished()` so it exits early if the model did not actually open the file.
- Add missing `page` config section loading from the config
- Fix visual line mode navigation to be more naturally
- Fix context menu on tabs not working
- Fix opening files in containers with already open file not loading.
- Fix `openSessionFromArray` function loading files incorrectly and breaking
- Fix segfault in `buildPageCache` because of double free of mupdf context
- Add implementation for `file_reload` command
- Fix synctex initialisation
- Fix synctex optional macro in the source code `HAS_SYNCTEX` -> `WITH_SYNCTEX`
- Fix image zoom anchoring
- `lua/Lektra.cpp`: Fix stack leak in `executeLuaCode` — the message-handler function was
  pushed before `luaL_loadstring` but never popped, growing the Lua stack by one slot on
  every call. Replaced with `lua_settop` save/restore and removed the broken handler
  (which returned 0 instead of the required 1 value). Also fixed `toStdString().c_str()`
  to a stable `QByteArray` local.
- `lua/view.cpp`: Fix `open` method reading `lua_upvalueindex(1)` with zero upvalues —
  caused undefined behaviour/crash. Now resolves the `Lektra` instance via
  `qobject_cast<Lektra*>((*view)->window())`.
- `lua/view.cpp`: Fix `fit`, `mode`, and `layout` getter methods returning `0` (no values)
  after pushing a value onto the stack — callers received garbage. Changed to `return 1`.
- `lua/view.cpp`: Fix `goto_location` applying a page-index offset twice: `pageno` was
  already converted to 0-based, then subtracted again before passing to `GotoLocation`,
  sending page 1 to index −1.
- `lua/view.cpp`: Fix `zoom` containing unreachable `return 0` after `return 1` in both
  branches. Collapsed to a single push + `return 1`.
- `lua/view.cpp`: Fix `set_invert` returning 1 in the success branch without pushing
  anything — now returns 0 (setter, no return value).
- `lua/view.cpp`: Replace empty stub bodies in `set_mode` and `save_as` with
  `luaL_error` so callers get a clear error instead of silent no-ops.
- `lua/event.cpp`: Fix `unregister` API mismatch — it expected `(string, int)` but
  `register` returns only an `int` handle. Unified to `(EventType, handle)` matching the
  `register` signature.
- `lua/event.cpp`: Fix `once` taking a string event name while `register` takes an integer
  `EventType`. Both now use integer `EventType` for consistency.
- `lua/event.cpp`: Fix `COUNT` sentinel exposed in the `lektra.event.EventType` Lua table
  due to an off-by-one `<=` in the population loop. Changed to `<`.
- `lua/event.cpp`: Fix `once` callbacks never being removed — `is_once` flag was set but
  `dispatchLuaEvent` never checked it, so the callback would fire on every subsequent
  dispatch (pushing nil after the first unref). Moved cleanup into `dispatchLuaEvent`
  using an erase-remove pass after invocation.
- `lua/event.cpp`: Fix iterator invalidation in `dispatchLuaEvent` — callbacks could call
  `unregister` during iteration. Now iterates over a local copy of the callback list.
- `lua/cmd.cpp`: Fix Lua registry reference leak on command unregister — `func_ref` was
  captured in the action lambda but `luaL_unref` was never called when the command was
  removed. Wrapped in a `shared_ptr` guard whose destructor calls `luaL_unref`.
- `include/DispatchType.hpp`: Fix duplicate `OnPageChanged` key in the dispatch map —
  the second entry silently overwrote the first in `QHash`.
- `include/DispatchType.hpp`: Rename reserved identifier `__dispatchEventMap` (double
  leading underscore is reserved by the C++ standard) to `s_dispatchEventMap`.
- `include/DispatchType.hpp`: Replace O(n) linear scan in `dispatchTypeToString` with an
  O(1) static array indexed by enum value.

### Performance

- `lua/view.cpp`: Implement `view:outline()` — returns the document table of contents as a
  recursive Lua table tree. Each entry has `title`, `pageno` (1-based, `nil` for external
  links), `x`, `y`, and a `children` array for nested headings.
- `Model.cpp` / `DocumentView.cpp`: Overhaul animated image (GIF) playback performance:
  - **Two-pass open**: `pingImages` reads frame count and per-frame delays from headers only
    (no pixel I/O). A single `[0]` scene read then decodes frame 0 and emits
    `openFileFinished` immediately, so the image appears without waiting for the full decode.
    Remaining frames are decoded in a background thread via `readImages` + `coalesceImages`.
  - **Remove dead `getAnimatedFrame`**: the function re-read and re-coalesced the entire file
    from disk on every call just to produce one frame. It was never called; removed entirely.
  - **Eliminate `m_image_cache` indirection for animated frames**: `setCurrentAnimFrame` now
    only updates `m_current_frame`; `requestImageRender` reads directly from
    `m_animated_frames[m_current_frame]`, removing a per-advance QImage copy.
  - **Remove unconditional `qDebug` in `setCurrentAnimFrame`**: the debug log fired on every
    frame advance (30+ times per second in all build configurations).
  - **Fix `cleanup_image` not resetting animated state**: `m_animated_frames`,
    `m_frame_delays_ms`, `m_frame_count`, and `m_current_frame` were left populated when
    switching away from an animated image.
  - **Frame-skip during startup**: the playback timer skips frames that have not been decoded
    yet (background decode still in progress) instead of displaying a blank frame.
  - **Elapsed-time frame scheduling**: the animation timer now subtracts actual render time
    from the next frame's nominal delay via `QElapsedTimer`, keeping the playback cadence
    on schedule even when individual frames render slowly.
  - **Free raw Magick frames early**: in the background decode path, the raw `readImages`
    vector is cleared immediately after `coalesceImages` so decoded-but-uncompressed Magick
    memory is released before the QImage conversion loop begins.

- `DocumentView.cpp`: Replace `QGraphicsItem::data(0).toString()` tag checks with `QSet<int>`
  membership tests (`m_placeholder_pages`, `m_preload_pages`) eliminating repeated
  `QVariant`→`QString` conversions in hot loops inside `removeUnusedPageItems`,
  `repositionPages`, and `renderPages`.
- `DocumentView.cpp`: Split the single render queue into a visible-page queue and a preload
  queue so `startNextRenderJob` dequeues in O(1) instead of doing an O(n) linear scan with
  an O(n) `removeAt` to find the next visible page to render.
- `DocumentView.cpp`: Eliminate `QHash::keys()` heap copies in `removeUnusedPageItems`,
  `clearVisibleLinks`, and `clearVisibleAnnotations`; replaced with direct iteration or a
  two-pass collect-then-delete approach.
- `DocumentView.cpp`: Cache the last rendered search-hit state (`index` + page-item pointer)
  in `updateCurrentHitHighlight` so the path is only recomputed when the hit or page item
  actually changes, avoiding redundant `QPainterPath` rebuilds on every scroll event.
- `Model.cpp`: Skip redundant `line_length()` traversal in `find_closest_in_page` for lines
  with no characters — the call always returned 0 so the `idx` increment was a no-op.
- `Model.cpp`: Hoist `m_inv_dpr` scale constant out of the per-link and per-annotation render
  loops in `renderPageWithExtrasAsync`; add `reserve` on `result.links` and
  `result.annotations` before those loops to avoid reallocation churn on pages with many
  links or annotations.
- `Lektra.cpp`: Hoist `findChildren<QShortcut *>()` out of the per-key loop in
  `setupKeybinding` — was performing a full child-object tree traversal once per key;
  now called once before the loop with deleted entries removed in-place to avoid dangling
  pointers.
- `Lektra.cpp`: Replace the temporary `QStringList() << "*.json"` filter construction in
  `getSessionFiles` with a `static const QStringList`, eliminating a heap allocation on
  every call.

### Breaking Changes

- Remove `placeholder_text` option from `[command_palette]` section of the config.
- Add `prompt` option in `[picker]` common to all pickers, which allows for a
  customizable prompt text shown next to the input field.
- Rename `always_open_in_new_window` to `single_instance`.
- C++ version requirement has been downgraded to C++20 (previously it was C++23) to allow
  for wider compatibility with different platforms and compilers, as C++20 is now widely
  supported by most modern compilers and platforms, while C++23 is still very new and not yet
  supported on many platforms.

## 0.7.0

### Features

- **Synctex** is now bundled with LEKTRA instead of using the system installed synctex,
  which should improve synctex support and make it available on all platforms without requiring users
  to install synctex separately. This can be disabled by passing `--without-synctex` in the configure script.
- Add optional `Imagemagick` image rendering library support for handling more image file formats.
    - **Requires ImageMagick to be installed on the system and `Magick++` development libraries for compilation.**
- Animated image support (e.g. animated GIFs, WEBP, AJPG) using `ImageMagick` for rendering.
- Hide unrelevant actions from the menu bar based on the file type of the currently opened document.

- Add `Windows` operating system support
- Add "pan" mouse action in `[mousebindings]` section.
    Used for panning around the page my clicking and dragging the mouse

    Example:

    ```toml
    [mousebindings]
    pan = "Alt+LeftButton"
    ```

- New actions in `[picker.keys]` section:
    - `expand`: if on a heirarchy node, expand it (in `flat_mode = false`) (default: `Tab`)
    - `collapse`: if on a heirarchy node, collapse it (in `flat_mode = false`) (default: `Tab`)
    - `section_prev`: move to the previous section (e.g. previous chapter in the outline picker) (default: `Ctrl+Shift+k`)
    - `section_next`: move to the next section (e.g. next chapter in the outline picker) (default: `Ctrl+Shift+j`)

- Default mouse bindings:
    - `pan` => `Alt+LeftButton`
    - `preview` => `Alt+Shift+LeftButton`
    - `portal` => `Ctrl+LeftButton`
    - `synctex_jump` => `Shift+LeftButton`

- Searching now searches from the current page/location to the end of the document.

### Bug Fixes

- Add ImageMagick to the `AboutDialog`'s `libraries used` section
- Stop animated image playback when not visible (different tab) to save resources and avoid unnecessary CPU usage.
- Hide mode, color and progress info in the statusbar when in non-supported file types (e.g. images) to avoid confusion
- Fix `invert color` menu button not working
- Make search behave more like in vim/emacs
- Fix linux `#ifdef`s
- Set minimum size for the `InputDialog` widget
- Remove redundant file dialog formats
- Picker `next`, `prev` navigation now goes through all the items instead of just the top level items when in `flat_mode = false` (hierarchical mode),
  which makes navigation in the picker more intuitive and consistent regardless of the mode.
- Picker not navigable when search bar is opened. (Issue reported by: [@linewaytin](https://codeberg.org/linwaytin))
- Fix `--check-config` not working with `[keybindings]`, `[mousebindings]` sections. (Issue reported by: [@lineick](https://github.com/lineick))
- Don't null out the statusbar item spacings which caused the statusbar items to have 0 padding and look weird.
- Fix `ColorDialog` not showing up the colored buttons.
- Make `RecentFilesPicker` be flat structured by default instead of hierarchical (it makes more sense to have flat structure)
- Handle `Esc` key to quit open pickers.

### Breaking Changes

- Remove `search_from_here` command as it's not useful anymore since the default behavior is to search from the current location.
- Move `sessions` and `last_pages.json` file to `QStandardPaths::AppDataLocation` for better compliance with platform standards for application data storage
  (previously stored in the `QStandardPaths::AppConfigLocation`, which is meant for configuration files).
- Remove LLM support (current implementation was not good), but it will be added back in the future with a better implementation.
- Removed shell scripts support as it was never implemented and there are no current plans to implement it.

#### Config changes:

- Organised `[statusbar]` components into it's own sections.
    - `[statusbar.component]` for the actual components to show in the statusbar and their order
        - `[statusbar.component.mode]` for the interaction mode component settings
        - `[statusbar.component.filename]` for the file name component settings
        - `[statusbar.component.zoom]` for the zoom indicator component settings
        - `[statusbar.component.pagenumber]` for the split indicator component settings
        - `[statusbar.component.progress]` for the link hint indicator component settings

- Ability to have configurable colors in the Color Dialog

```toml
[misc]
color_dialog_colors = [ "#FF500055", "#FF000055"] # Can have any number of colors
```

## 0.6.9

### Features

- Add some more spanish translations.
- Absolute vs Relative search hit jump
- Thumbnail panel for quick navigation and visual overview of the document pages. The thumbnail panel can be toggled with
the `thumbnail_panel` command and will show a scrollable list of page thumbnails on the side of the window.
Clicking on a thumbnail will navigate to that page in the main view.

- Default keybindings added for the following:
  - `split_horizontal` => `Ctrl+W,s`
  - `split_vertical` => `Ctrl+W,v`
  - `split_focus_left` => `Ctrl+W,h`
  - `split_focus_right` => `Ctrl+W,l`
  - `split_focus_up` => `Ctrl+W,k`
  - `split_focus_down` => `Ctrl+W,j`
  - `split_close` => `Ctrl+W,c`

- Add `--check-config` command line argument to check the validity of the config file and print any errors or warnings without launching the application.

### Config Options

- `flat_menu` option for `[outline]`, `[highlight_search]` picker. If `true`, the picker will
show all entries in a flat list without indentation, otherwise it will show entries in a
clickable node structure

- Ability to set `width` and `height` of pickers (outline, highlight search, recent files etc.)
    - These take either absolute pixel values (integers > 1) or fractional values in the range (0.0, 1.0] to
    size the picker relative to the window dimensions. These options apply globally to all picker
    types (outline, highlight, search, etc.); each picker type can override them individually in its own configuration section.

    For example:
    ```toml
    [picker]
    width = 0.3 # 30% of the window width
    height = 400 # 400 pixels height

    [outline]
    width = 0.25 # 25% of the window width for the outline picker
    ```

    Here, the default width for all pickers is set to 30% of the window width, but the outline picker specifically
    overrides this to be 25% of the window width. The height for all pickers is set to 400 pixels.

- Move synctex settings to their own section `[synctex]` for better organization and maintainability of the config file
    - `enabled` (bool): Enable/Disable synctex support
    - `editor_command` (string): Command to open the editor for synctex jump (e.g. `code --goto {file}:{line}:{column}`)
- `[thumbnail_panel]` section for thumbnail panel related settings
    - `show_page_number` (bool): Whether to show page numbers on the thumbnails
    - `panel_width` (float): Width of the thumbnail panel as a ratio of the main window width (e.g. 0.2 for 20% of the main window width)
- Ability to define multiple keybindings for same action using [TOML array](https://toml.io/en/v1.0.0#array)

Example:
```toml
[picker.keys]
up = [ "Ctrl+k", "Up" ]
down = [ "Ctrl+j", "Down" ]

[keybindings]
command_palette = [ ":", "Ctrl+P" ]
```

> [!NOTE]
> Notice that multiple keybindings use `[]` square brackets and not `{}` curly braces,
> which have a different meaning in [TOML](https://toml.io/en/v1.0.0#inline-table)

### Breaking Changes

- Removed `synctex_editor_command` from `[behavior]` section and moved it to `[synctex]` section as `editor_command`
- `dim_inactive` is now disabled by default (which was enabled previously out of the box)
- `confirm_on_quit` is now false by default (I feel it was annoying).
- Renamed config options:
    - `window_title` -> `title_format` in `[window]` section

### Bug Fixes

- Delete Annotation Command now remembers the annotation color (if set) or defaults to the current color.
- Fix `setZoom` and `setZoomAnchored` recursive calls which caused stack overflow and crashing when zooming in/out rapidly.
- **Fix memory leak due to not clearing up the `tracker` custom image device in MuPDF after rendering,
  which caused memory usage to grow indefinitely when navigating through pages.**
- **Fix memory leak due to wrong usage of `QFutureSynchronizer` which caused huge memory usage and
  slowdown when scrolling through pages.**
- Don't count thumbnail view in a container as a regular split when showing the total count of the splits in a container
- Fix file properties not working
- Fix crash when trying to save file after adding annotation.

### Core Changes

- Replaced Qt's `QColorDialog` with a custom color picker dialog for annotation color selection.
- Moved `Translations` directory to the root of the project (preivously it was in `src` directory).
- Moved `.hpp` header files into `include` directory (previously they were in `src` directory) for better organization
  of the codebase and to follow common C++ project structure conventions.
- Renamed `CommandPalette.shortcuts` to `CommandPalette.show_shortcuts` for better clarity of what the option does.

## 0.6.8

### Features
- Add **progressive searching** for search results. When a search query is entered, the search results will start showing up immediately as they are found, instead of waiting for the entire search to complete before showing any results. **This is enabled by default**.
- Annotation color change is now **undo-able**.

### Config Option
- `progressive` (bool) option in the `[search]` section of the config to enable/disable progressive searching.
- `highlight_matches` (bool) option in the `[search]` section of the config to enable/disable highlighting of search matches in the document. When enabled, all search matches will be highlighted in the document, otherwise only the current search match will be highlighted.
- `load_defaults` (bool) in `[keybindings]` section to load default keybindings. This allows users to choose whether they want to start with the default keybindings or start with an empty keybindings configuration, which can be useful for users who want to fully customize their keybindings without having to override the defaults.

### Command line argument:
- `--command` to execute one or more commands directly from the command line when launching LEKTRA. This allows users to quickly perform specific actions or set up their workspace with predefined commands without having to open the command palette after launch. Multiple commands can be executed by separating them with a semicolon (e.g. `lektra --command="toggle_fullscreen;search 'hello'"` to launch in fullscreen mode and open the config file immediately).
- `--list-commands` command line argument now prints the lektra commands in sorted order
- `--version` and `--list-commands` now output to stdout instead of stderr to be more consistent with standard command line tool behavior and to allow for easier parsing of the output when using these commands in scripts or other command line workflows.
- Lektra can now accept arguments directly from the command line when using `--command` argument, allowing for more flexible and powerful command execution. For example, you can now execute a command with specific arguments directly from the command line like this: `lektra --command="page_goto 5;"` to go to page 5 immediately after launching LEKTRA.

### Commands:
- `open_config` command to open the config file in the default text editor for easy editing and customization of settings.
- `scroll_down_half_page`, `scroll_up_half_page`, commands for more precise scrolling control, allowing users to scroll by half a page in the specified direction for better navigation through documents.
- `search_from_here` to start a search from the current page and position instead of the beginning of the document, which is useful for long documents when you want to search for something that appears after your current location without having to scroll all the way back to the beginning to start the search.
- `search_cancel` to cancel an ongoing search operation

### Optimizations

### Bug Fixes

- Fix page scrolling stuttering because of the `visual_line_mode` fix
- Fix file drag and drop not working
- Fix `visual_line_mode` rect offset when zooming in/out, which caused the visual line selection rectangle to be misaligned with the text when zoom level changes.
- Fix current search highlight not updating properly when zooming in/out or resizing the window, which caused the highlight to be in the wrong position or not visible after zooming or resizing.
- Fix picker hide on mainwindow with no tabs open. It caused no shortcuts to work because the mainwindow was no longer focused after the picker was shown once and then hidden.
- For real this time, fix the statusbar 0 padding

### Core Changes

- Make different types of Picker (e.g. outline picker, search highlights picker, recent files picker, etc.) inherit **configuration** options from the common base `Picker` class **configuration** to reduce code duplication and improve maintainability of the codebase.
- Make the same inheriting structure for `Annotation` **configuration**

## 0.6.7

### Features

- `OutlinePicker` now highlights the current page in the outline for better visual feedback on the current location in the document when navigating with the outline.
- Add `--list-commands` command line argument to print the list of available commands with their descriptions to the console and exit.
- Added `fade_duration` to `[jump_marker]` config section to configure the duration of the fade out effect for the jump marker in seconds.
- Finally added `Ctrl + Mouse Wheel` zooming!
- Config option for `[zoom]` to choose whether zoom should be anchored at mouse position or center of the viewport. This can be configured with the `anchor_to_mouse` option. Otherwise, zoom will be anchored to the center of the viewport as it was before.
- Added macOS support for LEKTRA. Check out [installation](https://dheerajshenoy.github.io/lektra/installation.html) instructions for more details. (Thanks to [@budingZou](https://codeberg.org/budingZou) for the PR!)
- Added custom input dialog with text wrapping for annotation comment input, which allows for better user experience when adding comments to annotations, especially for longer comments that require more space. The dialog will automatically adjust its size based on the content and provide a more comfortable interface for entering annotation comments.

### Bug Fixes

- Fix dismiss key not working in Pickers
- Don't allow searching in unsearchable file type (e.g. DJVU)
- Fix `selection_last` not working properly.
- Handle failed to open file properly by showing an error message and closing the tab cleanly.
- Fix file not focusing when opened from the recent files picker.
- Fix viewport center anchor zoom not centering on the actual center of the viewport. (Feels more natural now)
- Now pinch-zoom respects the mouse position anchor instead of the center of the viewport
- Fix spawning multiple instances of `AboutDialog`

### Core Changes

- Add man pages for command line usage documentation `man lektra`
- Rename `panel` to `statusbar`

## 0.6.6

### Features

- Add ability to not invert images in invert mode (valid in PDF, EPUB, MOBI, XPS, FB2) . Useful for users who want to use the invert mode for dark mode but don't want their images to be inverted. This can be configured in the settings with the `dont_invert_images` option under the `[behavior]` section of the config.
- Add search history to the search bar, allowing users to easily access their previous search queries and results. The search history can be navigated using the `up` and `down` arrow keys when the search bar is focused, and selecting a previous search query from the history will re-run that search and show the results.
- Ability to "preview" links by executing a mouse binding (Alt+LeftClick by default) on a link to open a temporary view of the link target without actually navigating to it in the current view in a floating window.

<p align="center">
  <img src="./images/preview.gif" alt="Preview Demo" width="600">
</p>

- "preview" config section
```toml
[preview]
size_ratio = {0.5, 0.5} # width and height ratio of the preview window compared to the main window
border_radius = 8 # border radius of the preview window in pixels
close_on_click_outside = true # whether to close the preview window when clicking outside of it
```
- Add `preview` command, which shows the preview window (if it was created already) for the current link target. The preview window will be created on the fly when the mouse binding is executed on a link for the first time, and will be reused for subsequent previews of the same link target.

- Add mousebindings to allow users to configure mouse interactions (e.g. mouse click, double click, right click, mouse wheel, etc.) with different modifiers (e.g. Ctrl, Shift, Alt) to trigger different mouse related commands.
    **Mouse commands**:
    - `portal` - open link in portal (ctrl + click on link)
    - `syntex_jump` - jump to source location in synctex enabled PDFs

- Add `backend` option in `[rendering]` section of the config to choose between "auto", "opengl" and "raster" rendering backends. The "auto" option will choose the best available backend based on the system capabilities, with a preference for OpenGL when available for better performance.

```toml
[rendering]
backend = "auto" # "auto", "opengl", "raster"
```

- Add `--tutorial` command line argument to open the tutorial PDF file directly from the command line without having to open the command palette and search for the command. This is useful for new users who want to quickly access the tutorial file and learn how to use LEKTRA.
- Add touchpad gesture support for zooming (pinch to zoom)

### Optimizations

- **Splitting view now takes you to the same page and location in the new split instead of the first page, which provides a more seamless experience when splitting the view to reference different parts of the same document side by side.**
- Improved text selection. Text selection is now more responsive and accurate, especially for multi-columned layouts. It should now be less jumpy and more stable when selecting text across columns and pages.

### Bug Fixes

- Fix Outline picker not going to the correct page and position when selecting an entry (if target location has NaN, then default to the center of the page)
- Fix portals and preview docs appearing blurry due to not using the correct DPR for rendering
- Fix remove item 0 warning in QGraphicsScene when closing document
- Fix double memory free crash on DJVU files
- Fix link hints mode not intercepting key presses properly
- Add all supported file types to the file open dialog filter
- Fix the QPainterPath pixel max limit error when text selection is too large by using page local coordinates instead of viewport coordinates for the selection path, which should allow for larger selections without hitting the pixel limit.
- Fix opening tutorial file not working due to wrong file path
- Fix djvu code under non-djvu builds causing compilation errors
- Fix optional dependency locator in `CMakeLists.txt`

### Breaking Changes

- Removed `grid` option from `[command_palette]` section of the config as it was not implemented.

## 0.6.5

### End User Features

- Add support for language translation and localization
- Support for running shell scripts
- Support for DJVU files (limited, only basic rendering and navigation, no text selection and searching)
- New command line argument `--layout` to specify the default layout mode on file open (e.g. `--layout=book` to open in book layout mode)
- Support for adding comments to annotations in LEKTRA. (After creating an annotation, you can add a note to it by right clicking on the annotation and selecting "Comment" from the context menu. This will open a dialog where you can enter your comment. Once added, the comment will be associated with the annotation.
**Note: These comments do not exist in the undo/redo stack, which means that if you change comment of an annotation and then undo, the comment change will not be undone. This is to do with limitations in the way the annotation data is stored and handled in the current implementation, but it might be improved in the future by integrating note changes into the undo/redo stack.**
- Annotations with comments _optionally_ can show a marker (e.g. an icon) to indicate that there's a note associated with the annotation, and hovering over the annotation will show the note as a tooltip (configurable in settings).
- Annotations have an _optional_ hover glow effect to provide visual feedback when hovering over annotations, making it easier to identify and interact with them.

### Core Features

- Picker widget now supports n columns
- `CommandManager` class to manage commands instead of general hash map, which allows for better organization and management of commands, as well as support for command descriptions, useful for documentation and purpose.

### Config Option

#### `[outline]`
    - `show_page_number` (bool)
    - `indent_width` (int)
### `dim_inactive` in `[portal]` - Dim inactive for portal views too
### `description` in `[command_palette]` - Show command description in the command palette if available
### New interaction mode `initial_mode="none"` in `[behavior]`, doesn't do anything.

### `[annotations]` - annotation related settings (common for all supported annotation types)
NOTE: This is just a convenience table to increase the readability of the config, it has no effect on its own, the actual annotation type tables (e.g. `[annotations.highlight]`, `[annotations.popup]`, etc.) still need to be configured separately for their respective annotation types.

### `[annotations.highlight]` - highlight annotation related settings
    - `hover_glow` (bool) to enable/disable hover glow effect on highlight annotation
    - `comment` (bool) to show the comment of the highlight annotation as tooltip on hover (if the annotation has a comment)
    - `comment_marker` (bool) to show a marker (e.g. an icon) on the annotation if it has a comment, to indicate that there's a comment associated with the annotation
    - `glow_width` (int) to set the width of the hover glow effect on highlight annotation
    - `glow_color` (RGBA hex value) to set the color of the hover glow effect on highlight annotation
    - `comment_font_size` (int) to set the font size of the comment tooltip for highlight annotations

### `[annotations.popup]` - popup annotation related settings
    - `hover_glow` (bool) to enable/disable hover glow effect on popup annotation
    - `glow_width` (int) to set the width of the hover glow effect on popup annotation
    - `glow_color` (RGBA hex value) to set the color of the hover glow effect on popup annotation

### `[annotations.rect]` - rectangle annotation related settings
    - `glow_width` (int) to set the width of the hover glow effect on rect annotation
    - `hover_glow` (bool) to enable/disable hover glow effect on rect annotation
    - `comment` (bool) to show the comment of the rect annotation as tooltip on hover (if the annotation has a comment)
    - `comment_marker` (bool) to show a marker (e.g. an icon) on the annotation if it has a comment, to indicate that there's a comment associated with the annotation
    - `glow_color` (RGBA hex value) to set the color of the hover glow effect on rect annotation
    - `comment_font_size` (int) to set the font size of the comment tooltip for highlight annotations

- `enabled` option in `[links]` to enable/disable links
### `[search]` for search related settings
    - `match_color` (RGBA hex value) to set the color of the search hit highlights
    - `index_color` (RGBA hex value) to set the color of the current search hit highlight

### Optimizations

- Pass `struct` const reference of only the respective members instead of the whole `Config` struct.

### New Commands

- `toggle_annot_comment_marker` - Toggle the comment marker on annotations that have comments (e.g. show/hide the icon indicating that there's a comment associated with the annotation)
- `none_mode` - Switch to none interaction mode where no interaction mode is active
- `picker_annot_comment_search` - Open the picker with the list of annotations that have comments, and searching by the comment text. Selecting an entry will jump to the annotation location.

### Breaking Changes

- Rename `LEFT_TO_RIGHT` layout to `horizontal` and `TOP_TO_BOTTOM` layout to `vertical` for better clarity and intuitiveness.
- Renamed `layout_left_to_right` command to `layout_horizontal` and `layout_top_to_bottom` command to `layout_vertical` to reflect the renamed layout modes.
- Command palette is now opened with ":" key by default instead of "Ctrl + Shift + P" to stay close to the Vim way of opening command palette and also because it's more ergonomic to open with a single key press.
- Renamed `[markers]` to `[jump_marker]`
- Removed `[color]` section from config and moved all colors to thier respective sections (e.g. annotation colors are now in `[annotations]` section, search hit colors are now in `[search]` section, etc.) for better organization and maintainability of the config file.


### Bug Fix

- Fix last page number loading
- Fix `[links]` section causing pages to not render in non-link supported files
- Fix page stride when zooming in DJVU files
- Fix zoom locking on the center of the viewport instead of the actual location
- Fix user shortcuts fighting with default shortcuts
- Fix zoom jumping to different page after zooming in/out.
- Fix annotation comment tooltip going off screen
- Fix color setting in the wrong format (ARGB instead of RGBA)
- Fix not able to save after adding comment
- Fix search results not clearing when searching for empty string
- Fix `-p,--page` command line argument not working
- Fix non-exsistent files trying to be opened
- Fix document reload causing unnecessary popup of message box saying it couldn't reload document
- Fix crash when reloading after file changes on disk
- Fix crash and infinite loop when trying to reload after file save
- Fix layout changing at runtime not working properly
- Fix crash due to zero image size in BOOK layout mode

## 0.6.4

### Bug Fix

- Fix memory leaks due to not freeing up the cloned MuPDF `fz_context`
- Fix `search` not working because of recent changes to page text caching
- Fix page getting cut off in layout modes other than BOOK
- Fix search navigation being all jumpy and causing head-ache (literally!)
- Fix SINGLE layout
- Fix LEFT_TO_RIGHT and TOP_TO_BOTTOM layout
- Fix highlighting crash due to context removal
- Fix split focus
- Fix `page_goto` dialog defaulting to page number 1 instead of current page number
- Fix `page_goto` requesting render even when the dialog is closed without input
- Fix multi click text selection

### Features

- Implement batch searching for faster search result fetching as soon as few searches are encountered.
- Regex searching
- Book (two-page) layout `[layout] { mode = "book" }`
- Ability to select text across page boundaries in layout modes with multiple pages visible
- Visual Line Mode
    - Added visual line mode for text selection that can be toggled with `visual_line_mode` command. In this mode, when you select a line of text, the selection will automatically extend to the beginning and end of the line, making it easier to select whole lines of text without having to precisely click at the start or end of the line.
    - Snaps when moving to different pages
    - mouse click in visual line mode to select the line

### New Commands

- Ability to set marks to locations in the document (both local and global level). Local marks are marks that are local to the document, whereas global marks are associated with particular document and when called will switch focus and go to the mark location in that document.
  - `mark_goto` - Ask user for the mark key and go to the location if key is valid
  - `mark_set` - Ask user for key to set the mark (local mark key start with lowercase letter or word and global starts with uppercase)
  - `mark_delete` - Ask user for the mark key to delete and delete if the key is valid
- `search_regex` - Opens searchbar with regex enabled
- `layout_book` - Book layout mode (Two-page layout)
- `mouse_follows_focus` - when enabled, the mouse will automatically move to the center of the focused view when switching focus between splits or portals. This is useful for users who want to keep their hands on the keyboard and avoid having to move mouse manually to the view they just focused.

### New Config Options

- `[portal]`
  - `enabled` (bool) : Enable portal (ctrl + click on link behavior)
  - `respect_parent` (bool): Close portal when the source view is closed
  - `border_width` (int): Width of the border around the view signifying that the view is a portal

### Optimizations

- Don't pass QPointF and QRectF as const references as it's useless.
- Cache `fz_stext_page*` (performance boost)

### Breaking Changes

- Renamed command:
  - `portal_focus` -> `portal`
  - Commands that begun with `toggle_` are renamed to just the action they do without the toggle prefix (e.g. `toggle_command_palette` -> `command_palette`, `toggle_fullscreen` -> `fullscreen`, etc.) because it's more intuitive to have the command name reflect the action it does rather than the implementation (i.e. whether it toggles or shows/hides explicitly)

- `startup_tab` widget is not enabled by default anymore

## 0.6.3

### Features

- Centralised `picker` widget for both `outline` and `search highlights` (and potentially other similar widgets in the future). This allows us to reuse the code for both features and also allows us to easily add new features that require a similar UI in the future.
- Orderless, space-aware searching ability for pickers by default (e.g. "open file", "file open" both will match "file_open" command)
- Add recent files picker (command: `files_recent`) to quickly access recently opened files
- Per page dimension support (instead of using the dimensions of the first page for the entire document, which causes issues with documents that have pages of different sizes)
- New config option
    - `[window]`
        - `initial_size` (array of 2 ints): Initial window size in pixels (width, height)
    - `[rendering]`
        - `antialiasing` (bool): Enable/Disable antialiasing for rendering (default: true)
        - `text_antialiasing` (bool): Enable/Disable antialiasing for text rendering (default: true)
        - `smooth_pixmap_transform` (bool): Enable/Disable smooth pixmap transformation when scaling (default: true)
    - `[behavior]`
        - `preload_pages` (int): Number of pages to preload before and after the current page for smoother navigation (default: 5)
    - `[selection]`
        - `copy_on_select` (bool): Copies selection to clipboard when text is selected (default: false)
- Moved the tab split count to the beginning of the tab instead of it being near the close button of the tab

### Optimizations

- Fix MessageBar's showMessage causing resizeEvent of GraphicsView causing re-rendering of displayed pages
- Make `text_cache` an LRU cache to reduce memory usage on large documents (used for text searching)
- Use page dimension cache (instead of repeatedely querying the document for dimension) for `toPDFSpace` and `toPixelSpace` functions to improve performance when converting coordinates between PDF space and pixel space, especially for documents with many pages or varying page sizes.
- Memory efficient QImage for page rendering and avoiding conversion to QPixmap. (Huge performance boost)

### Breaking Changes

- Remove `[outline]` and `[search_highlight]` sections from the config.
- Rename `[overlays]` to `[picker]` which is a more accurate name for what it does.
- Add `[picker.keys]` section for keybindings related to the picker navigation keys (which applies to all picker widgets like outline, search highlights, recent files, etc.)
- Removed panel settings from `[outline]` and `[search_highlight]` section as overlays are the only variant supported for now.
- Removed `config_auto_reload` from `[behavior]` as it was never supported.
- Removed `icc_color_profile` from `[rendering]` as it was never supported.

#### Renamed commands

- `yank` -> `selection_copy`
- `cancel_selection` -> `selection_cancel`
- `reselect_last_selection` -> `selection_last`
- `first_page` -> `page_first`
- `last_page` -> `page_last`
- `next_page` -> `page_next`
- `prev_page` -> `page_prev`
- `goto_page` -> `page_goto`
- `prev_location` -> `location_prev`
- `next_location` -> `location_next`
- `close_split` -> `split_close`
- `focus_split_left` -> `split_focus_left`
- `focus_split_right` -> `split_focus_right`
- `focus_split_up` -> `split_focus_up`
- `focus_split_down` -> `split_focus_down`
- `close_other_splits` -> `split_close_others`
- `focus_portal` -> `portal_focus`
- `save_session` -> `session_save`
- `save_as_session` -> `session_save_as`
- `load_session` -> `session_load`
- `text_select_mode` -> `selection_mode_text`
- `region_select_mode` -> `selection_mode_region`
- `open_file_tab` -> `file_open_tab`
- `open_file_vsplit` -> `file_open_vsplit`
- `open_file_hsplit` -> `file_open_hsplit`
- `open_file_dwim` -> `file_open_dwim`
- `close_file` -> `file_close`
- `reload` -> `file_reload`
- `encrypt` -> `file_encrypt`
- `save` -> `file_save`
- `save_as` -> `file_save_as`
- `tab1` -> `tab_1`
- `tab2` -> `tab_2`
- `tab3` -> `tab_3`
- `tab4` -> `tab_4`
- `tab5` -> `tab_5`
- `tab6` -> `tab_6`
- `tab7` -> `tab_7`
- `tab8` -> `tab_8`
- `tab9` -> `tab_9`
- `about` -> `show_about`
- `outline` -> `picker_outline`
- `highlight_annot_search` -> `picker_highlight_search`
- `command_palette` -> `toggle_command_palette`
- `tutorial_file` -> `show_tutorial_file`
- `show_startup` -> `show_startup_widget`
- `fullscreen` -> `toggle_fullscreen`
- `auto_resize` -> `fit_auto`
- `text_highlight_current_selection` -> `highlight_selection`

### New commands

- `reopen_last_closed_file` - Reopens the recently closed file in a new tab
- `copy_page_image` - Copy the current page as image to clipboard
- `file_decrypt` - Decrypt the current PDF file
- `annot_popup_mode` - Enter popup annotation creation mode
- `reshow_jump_marker` - Reshow the jump marker at the current location (useful if it was hidden after timeout)

### Optimizations

- Optimize `pageSceneAtPos` function to use a more efficient way of finding the page at a given position, which should improve performance when clicking on links or navigating to specific locations in the document.
- Maintain a vector for `LinkHint` pointers for efficient access and management of link hints, which should improve performance when showing and hiding link hints.
- Optimize `clearDocumentItems` function to efficiently clear all items related to a document when it's closed, which should improve performance and reduce memory usage when closing documents.

### Bug Fixes

- Fix tabs not respecting the `tabs.position` config option
- Fix for tab title not updated when closing split
- Fix for tab title not updated which changing current split focus

## 0.6.2

### Features
- Ability to create portals:
    - A portal is a view of the same document but viewing different page or location in the document. Portals are useful for referencing different parts of the same document side by side, for example when reading a research paper and wanting to view the references at the same time. (Ctrl + click on a link to open the target in a new portal)
- Ability to move tabs with mouse drag and drop within the tab bar and detach to new window if dropped outside
- Ability to split the view into multiple panes to view different pages of the same document (or different) side by side
- Ability to focus split with direction

- New flags for opening files in `vsplit` or `hsplit` directly from the command line
    - `--vsplit` - Open file in a vertically split pane
    - `--hsplit` - Open file in a horizontally split pane
- New config options for splits
    - `[split]`
        - `focus_follows_mouse` (bool): Whether to focus split pane on mouse hover
        - `dim_inactive` (bool): Whether to dim inactive split panes
        - `dim_inactive_opacity` (float, 0-1): Opacity level for dimming inactive panes (default: 0.5)
- New commands for splits
    - `split_horizontal` - Split the current view horizontally
    - `split_vertical` - Split the current view vertically
    - `close_split` - Close the current split pane
    <!-- - `focus_split_left`, `focus_split_right`, `focus_split_up`, `focus_split_down` - Move focus between split panes in the specified direction -->
    - `focus_split_left`, `focus_split_right`, `focus_split_up`, `focus_split_down` - Move focus between split panes in the specified direction (if no pane in that direction, do nothing)
    - `open_file_vsplit` - Open a file in a vertically split pane
    - `open_file_hsplit` - Open a file in a horizontally split pane
- New command for opening file
    - `open_file_dwim` - Open file with "Do What I Mean" behavior: if there's a tab open with no splits, open the file in new tab, if there's a tab open with splits, open the file in current split.

### Bug Fixes

- Fix tab drop opening new window instead of moving tab when dropped within the same window
- Fix window focus changing the dimmed state of split panes
- Fix crashing on splitting with huge documents.
- Remove filepath hash to keep track of already opened files, instead use the file path directly which should fix many other issues related to file opening and session restore
- Fix session loading layout restoration to work with the new splits system
- Fix `open_file_dwim` not working properly in some cases
- Fix callback not being called after file open in some cases (due to async file opening)
- Fix panel and tab not showing file info properly on file single file open
- Hide scrollbars when the entire document is visible in the viewport
- Fix not using config zoom factor
- Fit incorrect zoom on file open
- Fix rendering bug when there's no value for DPR set in the config
- Fix TabWidget logo font
- Fix zoom clamp range

### Breaking Changes
- Removed `ui` table in config and moved all options to their respective sections
    - `[ui.command_palette]` -> `[command_palette]`
    - `[ui.scrollbars]` -> `[scrollbars]`
    - `[ui.statusbar]` -> `[statusbar]`
    - `[ui.tabs]` -> `[tabs]`
    - `[ui.outline]` -> `[outline]`
    - `[ui.search_highlight]` -> `[search_highlight]`
    - `[ui.llm_widget]` -> `[llm_widget]`
    - `[ui.overlays]` -> `[overlays]`

## 0.6.1 [Lektra Update]
#### Renamed project name from `dodo` to `lektra`

### Features
- Add support for other MuPDF compatible file types: EPUB, XPS, CBZ, MOBI, FB2, SVG
- New Commands
    - `tab_move_left`, `tab_move_right` - move tabs in the tabBar left/right
- History navigation improvements
    - Forward/next-location history navigation with `next_location`
    - Preserve link source/target locations so jump markers land correctly
- Add `--about` command line argument to show about dialog

### Bug Fixes
- Disable access to annotation mode when not in PDF file (for other file formats)
- Remove PDF word from the file properties window title
- Fix text selection context menu takes precedence over annotation highlights when both apply
- Fix scrollbar auto-hide timer ignoring configured hide timeout after mouse leave
- Fix recent files page restore to open the stored page without adding history entries

### Breaking Changes

- Remove `close_file` command as it was redundant with `tab_close` (which does the same thing)
- Rename `bar_position` to `location` in `[ui.tabs]`

## 0.6.0

### Features

- Add ability to copy unformatted text (removes hyphenation and joins lines)
- Add delete annotations implementation when in `annot_select_mode`
- Increased rendering performance.
- Add `tutorial_file` command to show a tutorial PDF file
- Added `role.txt` system prompts for the LLM
- Implement LRU (Least Recently Used) cache for reduced memory usage
    - Respect `behavior.cache_pages` config option for this
- Add frame border around the overlay widgets
- Replace `setdpr` command with `set_dpr`. This command now opens a popup input dialog asking for the new Device Pixel Ratio (DPR) value.
- Drag-drop file handling
    - Hold `Shift` while dropping a file onto the main view to open in a new window
- Improved Startup Page
- `goto_page` command now registers current location for history navigation back to the page from where the command was called.

### Config options
- Page foreground and background color config option
    - `[colors]`
        - `page_foreground` (RGBA hex value)
        - `page_background` (RGBA hex value)
- Tab close commands `[keybindings]`
    - `tabs_close_left` - close all tabs to the left of the current tab
    - `tabs_close_right` - close all tabs to the right of the current tab
    - `tabs_close_others` - close all tabs except the current tab
- New options for tabs `[ui.tabs]`
    - Lazy loading `lazy_load` (bool) - loads only when switching to the tab (useful when loading lots of papers)
    - File path in tab `[ui.tabs]`
        - `full_path` - whether to show full path in the tab
- Overlay frame config `[ui.overlays]`
    - `border` - show border around overlay frames
    - `shadow` table
        - `enabled` - toggle shadow on overlay frames
        - `blur_radius` - shadow blur radius in pixels
        - `offset_x` - horizontal shadow offset
        - `offset_y` - vertical shadow offset
        - `opacity` - shadow opacity (0-255)

### Optimizations

- Set optimization flag and CacheMode for QGraphicsView
- Colors are now stored as packed RGBA ints and parsed from hex config values

### Breaking Changes

**NOTE**: You might have to change few things in the config

- Command `first_tab`, `last_tab` renamed to `tab_first`, `tab_last`
- Remove `Fit None` from the fit menu
- Moved `Auto Resize` checkbox to the view menu instead of fit menu

### Bug Fixes

- Fix startup page layout
- Fix orderless completion not working in the command palette
- Rename startup tab to `Startup Page`
- Fix double free in document cleanup
- Fix link hover cursor change
- Fix link navigation when target page is not yet rendered
- Hide the command palette before executing the command
- Fix command pallette showing only set keybound commands
- Fix tab close commands not working as expected
- Fix cursor not changing when hovering over link
- Fix right click context menu on tabs
- Command palette orderless matching consider underscore as literal when entered.
- Cancel page rendering request when starting to scroll
- Placeholder page items are just upscale pixmaps to reduce cpu and memory usage
- Render visible pages instead of just reloading single page when file changes in disk
- Don't cache `fz_stext_page` (very memory intensive)
- LLM Widget
    - Disable send button in the LLM widget when there's no query
    - Message when provider server is not found to be running
- Fix scroll commands not triggering scrollbar visibility (for auto_hide = true)
- Fix document not reloading even when `[behavior.auto_reload]` is set to true
- Hide searchbar when the search term is empty
- Fix loading default config when error is found in config
- Fix highlights search overlay hiding after selecting entry

## 0.5.6

### Bug Fixes

- Fix dodo not compiling without synctex

## 0.5.5

### Features

- Change color of selected annotations
- Allow editing popup annotations (also called text annotations)
- Visual feedback when hovering over popup annotation icons
- Statusbar config option
    - `[ui.statusbar]`
        - `visible` (bool): Show/Hide statusbar
        - `padding` (array of 4 ints): Padding around the statusbar (top, right, bottom, left)
        - `show_session_name` (bool): Show session name in statusbar
        - `file_name_only` (bool): Show only file name instead of full path
        - `show_file_info` (bool): Show file info (size, number of pages) in statusbar
        - `show_page_number` (bool): Show current page number in statusbar
        - `show_mode` (bool): Show current mode (e.g. selection mode) in statusbar
        - `show_progress` (bool): Show reading progress in statusbar
- Percentage progress indicator in statusbar

### Bug Fixes

- Fix rect annotation creation
- Fix popup annotation creation

## 0.5.4

### Features

- **Command palette improvements**
    - Sort commands alphabetically
    - Right-align shortcuts and optionally hide them (`[ui.command_palette].show_shortcuts`)

- **Overlay scrollbars**: Scrollbars now appear as floating overlays that don't shift the layout
    - Auto-hide after configurable timeout (default 1500ms)
    - Scrollbars remain visible while dragging or hovering over them
    - Scrollbars flash on zoom in/out/reset
    - New config options in `[ui.scrollbars]`:
        - `auto_hide` (bool): Auto hide scrollbar when not in use
        - `size` (int): Scrollbar width/height in pixels (default: 12)
        - `hide_timeout` (int): Milliseconds before hiding after inactivity (default: 1500)

- **Detach from terminal** - By default dodo will now detach from the terminal when launched from a terminal. This can be disabled by using the `--foreground` command line argument.

- **Add LLM support \[OPTIONAL\]**
    - LLM (Large Language Model) integration is entirely optional and can be disabled completely from the code by the compile flag `ENABLE_LLM_SUPPORT`.
    - Integrate with local LLM models to provide AI-powered assistance
    - Config options:
        - `[llm]`
            - `enabled` (bool): Enable/Disable LLM assistance
            - `model_path` (string): Path to the local LLM model
            - `temperature` (float): Sampling temperature for response generation
            - `max_tokens` (int): Maximum tokens in the generated response

- **Region Selection context menu**
    - Right click context menu when in region selection mode
        - Copy text in region to clipboard
        - Copy region as image to clipboard
        - Save region as image to file
        - Open region as image externally


### Bug Fixes

- Fix scrollbar disappearing while actively dragging
- Fix scrollbar hiding when quickly changing scroll direction
- Fix scrollbar handle size not updating after zoom
- Add visual feedback to current keypress in the link hinting mode
- Hide non-matching link hints as keys are entered and dim the typed digits
- Fix config color parsing to treat 8-digit hex values as RGBA
- Add orderless, space-aware command palette search

## 0.5.3


### Features


- **Cross-window tab drag and drop**: Tabs can now be dragged between windows or detached to create new windows.
- Change cursor when selecting or highlighting text
- Searchable text highlights
- Outline widget and Search Highlight widget types - "dialog", "side_panel", "overlay" (configurable in settings)
    - UI
        `[ui.outline]` - `type=<value>` where value = `dialog`, `side_panel`, `overlay`
        `[ui.search_highlight]` - `type=<value>` where value = `dialog`, `side_panel`, `overlay`
- Detection of non-link URL pdf object in the PDF and making them clickable links (optional, configurable in settings)
    - [ui.links]
        - `detect_urls` - Enable/Disable non-link URL detection and linkification
        - `url_regex` - Custom regex for URL detection (default is a standard URL regex)

- New options in `[behavior]`:
    - `cache_pages` (int): Maximum number of pages to keep cached per document. Default is `20`.
    - `always_open_in_new_window` (bool): If true, files will always open in a new window instead of a new tab. Default is `false`.



### Bug Fixes
- Remove stray return causing panel update issues
- Fix prompting for password protected documents
- Fix session loading not opening files properly - files now load correctly with their saved state (page, zoom, fit mode, invert color)
- Fix session page restoration using wrong page index (1-indexed vs 0-indexed)
- Fix early return in `openFileFinished` callback that prevented the callback from being executed for documents with outlines
- Fix context menu not showing at the right position
- Fix fit width not working properly in some cases
- Fix startup Page widget not respecting fit mode from config
- Fix scrolling to specific position and center it in the viewport
- Fix zoom changing the viewport position unexpectedly
- Show highlight search overlay properly
- Fix double memory free crash on exit when there's more than one tab
- Show information if no outline is present in the document instead of empty outline panel
- Fix crash when rendering link/annotation items from background threads

## 0.5.2

- Add runtime layout switching
- Link hinting mode for keyboard link navigation
- Updated organized configuration settings (check config.toml)
- Fix first page and last page offset in LEFT_TO_RIGHT and TOP_TO_BOTTOM layouts
- Make scrollbar handle transparent to allow search hits marker visibility
- Show placeholder page when pages are loading
- Use queue for managing threads while rendering instead of spawning threads for each render request
- Make asynchronous to avoid UI blocking on large documents
- Add min and max zoom levels
- Add Link Keyboard hinting mode
- Show loading spinner when loading huge documents
- Add rotation support
- Fix annotation deleting
- Fix panel name modified status not updating properly
- Fix annotation select at point and rectangle selection
- Fix highlight annotation rendering issues
- Fix highlight annotation coordinate mismatch

### Bug Fixes

- Fix invert color not reloading already visible pages
- Fix LEFT_TO_RIGHT layout not showing vertical scrollbar
- Fix link clicking
- Fix context menu positioning


## 0.5.1

### Features
- Search hits marker on the scrollbar
- Config option to enable/disable search hits marker on scrollbar
    - UI
    `search_hits_on_scrollbar`
- Search is now smart case by default (i.e. case insensitive unless uppercase letters are used in the search query)
- Search Bar improvements
    - Show only icons and not text in the search bar buttons
- Close file in tab
- Preload next and previous pages for faster navigation (configurable in settings)
    - Rendering
        `preload_adjacent_pages`
        `num_preloaded_pages`
- Layouts
    - Single Page
    - Left to Right
    - Top to Bottom
- Config option to set default layout on file open
    - UI
        `layout` - values: `single`, `left-to-right`, `top-to-bottom`

### Bug Fixes

- Click on links not working properly in some cases
- Fix navigation from menu
- Fix synctex to work with new rendering system
- Significantly improved rendering performance by optimizing memory copying from MuPDF to Qt
- Fix go back history not working properly
- Single page layout issues
    - Link item deletion
    - Page navigation
- History navigation issues
## 0.5.0

### Features
- Switch from single view rendering to tile based rendering like sane PDF viewers
- Huge rewrite of the rendering system
    - Asynchronous rendering
    - Improved performance


### Bug Fixes
- Fix annotation rendering issues
- Fix annotation position issues on zoom and pan

## 0.4.3
- Zoom render debounce to improve performance
- Make fit window, fit width, fit height work properly with the new debounce logic

## 0.4.2
- Add popup annotation menu item
- Edit last pages widget disable when no entries exist
- Don't render highlight annotations as it's done by MuPDF
- Merge highlight annotations on overlapping areas


## 0.4.1
- Remove padding around the document view
- Fix installation script
- Fix build issues
- Fix .desktop file installation
- Fix skinny statusbar
- Fix panel file name display (absolute vs relative path)
- Fix session file overwriting without prompt
- Fix session path to file
- Add session button to panel
- Fix session button label
- Fix synctex not working after reloading file
- Update session files if in session on application exit
- Fix loading session opening second window on launch

## 0.4.0

- Implement document reloading on external changes
- Remove command bar
- Implement clean search bar UI

## 0.3.1

### Features

- Undo/Redo support for annotations
    - Rect annotations
    - Text highlight annotations
    - Delete annotations
- Multi-click text selection (double, triple, quadruple click for word, line, paragraph)
- Page rotation (clockwise and counter-clockwise)
- Editable page number in panel (click to edit and navigate)
- Tab navigation keybindings (tab_next, tab_prev, tab1-tab9)
- Config options:
    - Behavior
        - `undo_limit` - Maximum number of undo operations to keep

### Bug Fixes

- Jump marker fading animation fix
- Fix unsaved changes being asked multiple times
- Centralized unsaved handling

## 0.3.0

### Features

- Fix copy text popup when no text is selected
- Wonky selection handling
- Fix scaling problem in single monitor systems
- Multi-monitor user defined scaling support
- Region select mode
    - Copy text from selected region to clipboard
    - Copy selected region as image to clipboard
    - Save selected region as jpeg/png etc. image
    - Open selected region externally
- Image right click context menu
    - Copy image to clipboard
    - Save image as jpeg/png/psd
    - Open image externally
- Display Pixel Ratio user config option (fix blurry text on high DPI screens)
- Outline widget as a sidebar option
- Outline widget page numbers displayed properly
- Outline hierarchy search support
- Config options:
    - UI
        - `outline_as_side_panel`
        - `outline_panel_width`
        - `link_hint_size`
    - Rendering
        - `dpr`
- Popup annotations improved

### Bug Fixes

- Fix buggy selection behavior after clicking
- Fix blurry text on high DPI screens
- Fix popup annotation not deleting properly

## 0.2.4

- Fix drag and drop not opening files
- Popup Annotation support
- Page navigation menu disable unintended buttons

## 0.2.3

- Fix startup tab on launch with files
- Vim-like search in commandbar
- Goto page from commandbar
- Fix goto page
- Fix link clicking mouse release event
- Fix recent files addition
- Fix panel and other actions updating on tab switching

## 0.2.2

- Tabs support
- Sessions support

## 0.2.1

- Bug fixes

## 0.2.0

- Page visit history
- Link visit
    - http/https links
    - Page links
    - XYZ links
- Browse links implemented (URL)
- Basic rectangle annotations
- Search
- Table of Contents
- File properties
- Keyboard link navigation


## 0.1.0

- Render
- Navigation
- Panel
