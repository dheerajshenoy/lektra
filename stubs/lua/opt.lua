---@meta

lektra = lektra or {}
lektra.opt = {}

---@class Screen
---@field description string Description of the screen/monitor (e.g. "desc:AU Optronics 0xE3AC").
---@field scale number Scale factor for the screen (e.g. 1.0 for standard DPI, 1.5 for HiDPI).

-- ── Enums ────────────────────────────────────────────────────────────────────
-- Note: enum tables live on `lektra`, not `lektra.opt`.

---@enum LayoutMode
lektra.LayoutMode = {
    Vertical   = 0,
    Horizontal = 1,
    Single     = 2,
    Book       = 3,
}

---@enum FitMode
lektra.FitMode = {
    Width  = 0,
    Height = 1,
    Window = 2,
}

---@enum MouseButton
lektra.MouseButton = {
    Left   = 1,
    Right  = 2,
    Middle = 4,
}

---@enum Backend
lektra.Backend = {
    Auto = 0,
    Raster = 1,
    OpenGL = 2,
}

-- ── Section class definitions ─────────────────────────────────────────────────
-- Each section is a proxy table; fields are read/written directly as properties.
-- Color fields are packed 32-bit ARGB integers (Qt format).

---@class OptPage
---@field bg integer Background color (ARGB integer).
---@field fg integer Foreground color (ARGB integer).
lektra.opt.page = {}

---@class OptSynctex
---@field editor_command string Command used to open the source editor.
---@field enabled boolean Whether SyncTeX support is active.
lektra.opt.synctex = {}

---@class OptSearch
---@field absolute_jump boolean Jump to the match page even if already on it.
---@field highlight_matches boolean Highlight all search matches.
---@field index_color integer Color of the current match highlight (ARGB integer).
---@field match_color integer Color of non-current match highlights (ARGB integer).
---@field progressive boolean Enable progressive (incremental) search.
lektra.opt.search = {}

---@class OptAnnotationsHighlight
---@field color integer Highlight annotation color (ARGB integer).
---@field comment boolean Show comment text for highlights.
---@field comment_font_size integer Font size of the comment text.
---@field comment_marker boolean Show the comment marker icon.
---@field glow_color integer Glow color around the highlight (ARGB integer).
---@field glow_width integer Width of the glow effect in pixels.
---@field hover_glow boolean Show glow only on hover.

---@class OptAnnotationsRect
---@field color integer Rectangle annotation color (ARGB integer).
---@field comment boolean Show comment text for rectangles.
---@field comment_font_size integer Font size of the comment text.
---@field comment_marker boolean Show the comment marker icon.
---@field glow_color integer Glow color around the rectangle (ARGB integer).
---@field glow_width integer Width of the glow effect in pixels.
---@field hover_glow boolean Show glow only on hover.

---@class OptAnnotationsPopup
---@field color integer Popup annotation color (ARGB integer).
---@field comment boolean Show comment text for popups.
---@field comment_font_size integer Font size of the comment text.
---@field glow_color integer Glow color around the popup (ARGB integer).
---@field glow_width integer Width of the glow effect in pixels.
---@field hover_glow boolean Show glow only on hover.

---@class OptAnnotations
---@field highlight OptAnnotationsHighlight
---@field rect OptAnnotationsRect
---@field popup OptAnnotationsPopup
lektra.opt.annotations = {
    ---@type OptAnnotationsHighlight
    highlight = {},
    ---@type OptAnnotationsRect
    rect = {},
    ---@type OptAnnotationsPopup
    popup = {},
}

---@class OptThumbnailPanel
---@field show_page_numbers boolean Show page numbers under thumbnails.
---@field panel_width integer Width of the thumbnail panel in pixels.
lektra.opt.thumbnail_panel = {}

---@class OptPortal
---@field border_color integer Portal border color (ARGB integer).
---@field border_width integer Portal border width in pixels.
---@field dim_inactive boolean Dim inactive portals.
---@field respect_parent boolean Portal respects the parent view's zoom/fit.
---@field enabled boolean Whether portals are enabled.
lektra.opt.portal = {}

---@class OptWindow
---@field bg integer Window background color (ARGB integer).
---@field accent integer Accent color (ARGB integer).
---@field fullscreen boolean Start in fullscreen mode.
---@field menubar boolean Show the menu bar.
---@field startup_tab boolean Open a new tab on startup.
---@field title_format string Printf-style format string for the window title.
---@field initial_size integer[] Two-element array `{width, height}` for the initial window size.
lektra.opt.window = {}

---@class OptLayout
---@field initial_fit FitMode Fit mode applied when a document is first opened.
---@field auto_resize boolean Automatically resize the view to fit when the window resizes.
---@field mode LayoutMode Page layout mode (single, vertical, horizontal, book).
lektra.opt.layout = {}

---@class OptStatusbar
---@field padding integer[] Four-element array `{top, right, bottom, left}` padding in pixels.
---@field visible boolean Whether the status bar is shown.
lektra.opt.statusbar = {}

---@class OptZoom
---@field anchor_to_mouse boolean Zoom anchored to the mouse cursor position.
---@field factor number Zoom step multiplier applied per scroll tick.
---@field level number Current zoom level (1.0 = 100%).
lektra.opt.zoom = {}

---@class OptSelection
---@field color integer Text selection highlight color (ARGB integer).
---@field copy_on_select boolean Automatically copy selected text to clipboard.
---@field drag_threshold integer Pixel distance before a drag is recognized.
lektra.opt.selection = {}

---@class OptSplit
---@field dim_inactive boolean Dim the inactive split pane.
---@field dim_inactive_opacity number Opacity of the inactive pane when dimmed (0.0–1.0).
---@field focus_follows_mouse boolean Focus a split pane when the mouse enters it.
---@field mouse_follows_focus boolean Warp the mouse to the focused pane.
lektra.opt.split = {}

---@class OptScrollbars
---@field auto_hide boolean Automatically hide scrollbars when idle.
---@field hide_timeout number Seconds of inactivity before scrollbars hide.
---@field horizontal boolean Show the horizontal scrollbar.
---@field search_hits boolean Show search hit markers on the scrollbar track.
---@field size integer Scrollbar track width in pixels.
---@field vertical boolean Show the vertical scrollbar.
lektra.opt.scrollbars = {}

---@class OptJumpMarker
---@field color integer Jump marker color (ARGB integer).
---@field enabled boolean Whether jump markers are shown.
---@field fade_duration number Duration in seconds for the fade-out animation.
lektra.opt.jump_marker = {}

---@class OptLinks
---@field boundary boolean Draw a boundary box around detected links.
---@field detect_urls boolean Detect plain-text URLs as clickable links.
---@field enabled boolean Whether link detection and navigation is active.
---@field url_regex string Regular expression used to detect URLs.
lektra.opt.links = {}

---@class OptLinkHints
---@field bg integer Link hint background color (ARGB integer).
---@field fg integer Link hint foreground/text color (ARGB integer).
---@field size number Font size for link hint labels.
lektra.opt.link_hints = {}

---@class OptTabs
---@field auto_hide boolean Hide the tab bar when only one tab is open.
---@field closable boolean Show a close button on each tab.
---@field full_path boolean Display the full file path as the tab title.
---@field lazy_load boolean Defer loading of background tabs until they are activated.
---@field movable boolean Allow tabs to be dragged and reordered.
---@field visible boolean Whether the tab bar is shown.
lektra.opt.tabs = {}

---@class OptPicker
---@field width number Picker widget width (as a fraction of the window or in pixels).
---@field height number Picker widget height.
---@field border boolean Draw a border around the picker.
---@field alternating_row_color boolean Alternate row background colors in the picker list.
lektra.opt.picker = {}

---@class OptOutline
---@field indent_width integer Pixels to indent each outline level.
---@field show_page_number boolean Show the page number next to each outline entry.
---@field flat_menu boolean Display the outline as a flat list instead of a tree.
lektra.opt.outline = {}

---@class OptHighlightSearch
---@field flat_menu boolean Display search results as a flat list.
lektra.opt.highlight_search = {}

---@class OptCommandPalette
---@field prompt string Placeholder text shown in the command palette input.
---@field vscrollbar boolean Show a vertical scrollbar in the command palette.
---@field show_shortcuts boolean Show keyboard shortcuts next to commands.
---@field description boolean Show command descriptions in the palette.
lektra.opt.command_palette = {}

---@class OptRendering
---@field antialiasing boolean Enable antialiasing for page rendering.
---@field antialiasing_bits integer Number of multisampling bits for antialiasing (e.g. 4, 8).
---@field text_antialiasing boolean Enable text-specific antialiasing.
---@field smooth_pixmap_transform boolean Use smooth (bilinear) scaling for pixmaps.
---@field backend Backend Rendering backend
---@field scale number|Screen Scale factors for different device pixel ratios (e.g. `{ [1] = 1.0, [2] = 1.5 }`).
lektra.opt.rendering = {}

---@class OptBehavior
---@field confirm_on_quit boolean Show a confirmation dialog before quitting.
---@field undo_limit integer Maximum number of undo steps.
---@field cache_pages integer Number of rendered pages to keep in the page cache.
---@field preload_pages integer Number of pages to pre-render ahead of the current page.
---@field auto_reload boolean Automatically reload the document when the file changes on disk.
---@field invert_mode boolean Start with colour inversion enabled.
---@field dont_invert_images boolean Exclude images from colour inversion.
---@field open_last_visited boolean Reopen the last visited document on startup.
---@field single_instance boolean Enforce a single application instance.
---@field remember_last_visited boolean Remember and restore the last visited page.
---@field recent_files boolean Track recently opened files.
---@field num_recent_files integer Maximum number of recent files to remember.
---@field page_history_limit integer Maximum number of entries in the page-navigation history.
lektra.opt.behavior = {}

---@class OptPreviewSizeRatio
---@field width number Width of the preview as a fraction of the window width.
---@field height number Height of the preview as a fraction of the window height.

---@class OptPreview
---@field size_ratio OptPreviewSizeRatio Table with `width` and `height` ratio fields.
---@field border_radius integer Corner radius of the preview popup in pixels.
---@field close_on_click_outside boolean Close the preview when clicking outside it.
---@field opacity number Opacity of the preview popup (0.0–1.0).
lektra.opt.preview = {}

---@class OptMisc
---@field color_dialog_colors string[] Preset colours shown in the colour picker (e.g. `"#FF112233"`).
lektra.opt.misc = {}
