---@meta

lektra = lektra or {}
---Global options. Setting one changes the default for views created later and the current view; other open views keep their own values (see `View:opt()`). Like Vim's `:set`. `lektra.opt_global` only changes the default (`:setglobal`), `View:opt()` only one view (`:setlocal`).
lektra.opt = {}

--- A color: a string `"#RRGGBBAA"` or `"#RRGGBB"` (opaque), or an integer `0xRRGGBBAA` (red in the top byte, alpha in the lowest).
---@alias Color integer|string

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
    Width       = 0,
    Height      = 1,
    Window      = 2,
    -- "Smart" fit modes ignore blank page margins and fit the tight
    -- content bounding box instead. See `fit_width_smart` / `fit_height_smart`.
    WidthSmart  = 3,
    HeightSmart = 4,
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
-- Color fields are packed 32-bit `Color`s (Qt format).

---@class OptPage
---@field bg? Color Background color
---@field fg? Color Foreground color
lektra.opt.page = {}

---@class OptSynctex
---@field editor_command? string Command used to open the source editor.
---@field enabled? boolean Whether SyncTeX support is active.
lektra.opt.synctex = {}

---@class OptSearch
---@field absolute_jump? boolean Jump to the match page even if already on it.
---@field highlight_matches? boolean Highlight all search matches.
---@field index_color? Color Color of the current match highlight (`Color`).
---@field match_color? Color Color of non-current match highlights (`Color`).
---@field progressive? boolean Enable progressive (incremental) search.
lektra.opt.search = {}

---@class OptAnnotationsHighlight
---@field color? Color Highlight annotation color (`Color`).
---@field comment? boolean Show comment text for highlights.
---@field comment_font_size? integer Font size of the comment text.
---@field comment_marker? boolean Show the comment marker icon.
---@field glow_color? Color Glow color around the highlight (`Color`).
---@field glow_width? integer Width of the glow effect in pixels.
---@field hover_glow? boolean Show glow only on hover.

---@class OptAnnotationsRect
---@field color? Color Rectangle annotation color (`Color`).
---@field comment? boolean Show comment text for rectangles.
---@field comment_font_size? integer Font size of the comment text.
---@field comment_marker? boolean Show the comment marker icon.
---@field glow_color? Color Glow color around the rectangle (`Color`).
---@field glow_width? integer Width of the glow effect in pixels.
---@field hover_glow? boolean Show glow only on hover.

---@class OptAnnotationsPopup
-- Popup annotations inherit only from Base — they do NOT have a `color`
-- field (unlike Highlight and Rect).
---@field comment? boolean Show comment text for popups.
---@field comment_font_size? integer Font size of the comment text.
---@field glow_color? Color Glow color around the popup (`Color`).
---@field glow_width? integer Width of the glow effect in pixels.
---@field hover_glow? boolean Show glow only on hover.

---@class OptAnnotations
---@field highlight? OptAnnotationsHighlight
---@field rect? OptAnnotationsRect
---@field popup? OptAnnotationsPopup
lektra.opt.annotations = {
    ---@type OptAnnotationsHighlight
    highlight = {},
    ---@type OptAnnotationsRect
    rect = {},
    ---@type OptAnnotationsPopup
    popup = {},
}

---@class OptThumbnailPanel
---@field show_page_numbers? boolean Show page numbers under thumbnails.
---@field panel_width? integer Width of the thumbnail panel in pixels.
lektra.opt.thumbnail_panel = {}

---@class OptPortal
---@field border_color? Color Portal border color (`Color`).
---@field border_width? integer Portal border width in pixels.
---@field dim_inactive? boolean Dim inactive portals.
---@field respect_parent? boolean Portal respects the parent view's zoom/fit.
---@field enabled? boolean Whether portals are enabled.
lektra.opt.portal = {}

---@class OptWindow
---@field bg? Color Window background color (`Color`).
---@field accent? Color Accent color (`Color`).
---@field fullscreen? boolean Start in fullscreen mode.
---@field menubar? boolean Show the menu bar.
---@field show_menu_icons? boolean Show standard icons next to menubar items (toggles Qt::AA_DontShowIconsInMenus).
---@field startup_tab? boolean Open a new tab on startup.
---@field title_format? string Printf-style format string for the window title.
---@field initial_size? integer[] Two-element array `{width, height}` for the initial window size.
lektra.opt.window = {}

---@class OptLayout
---@field initial_fit? FitMode Fit mode applied when a document is first opened.
---@field auto_resize? boolean Automatically resize the view to fit when the window resizes.
---@field mode? LayoutMode Page layout mode (single, vertical, horizontal, book).
---@field spacing? integer Gap between pages in pixels.
lektra.opt.layout = {}

---@class OptStatusbarComponentMode
---@field icon? boolean Show the interaction mode as an icon.
---@field show? boolean Whether the interaction-mode indicator is shown at all.
---@field text? boolean Show the interaction mode as text (e.g. "Select", "Pan").

---@class OptStatusbarComponentPageNumber
---@field show? boolean Show the current page number.

---@class OptStatusbarComponentSession
---@field show? boolean Show the active session name.

---@class OptStatusbarComponentZoom
---@field show? boolean Show the current zoom level.

---@class OptStatusbarComponentFileName
---@field full_path? boolean Show the full file path instead of just the name.
---@field show? boolean Show the file name.

---@class OptStatusbarComponentProgress
---@field show? boolean Show the reading-progress bar.

---@class OptStatusbarComponents
---@field mode? OptStatusbarComponentMode
---@field pagenumber? OptStatusbarComponentPageNumber
---@field session? OptStatusbarComponentSession
---@field zoom? OptStatusbarComponentZoom
---@field filename? OptStatusbarComponentFileName
---@field progress? OptStatusbarComponentProgress

---@alias StatusbarModule "session"|"filename"|"page"|"zoom"|"progress"|"mode"|"portal"|"narrow"

---An item of the statusbar layout: a module name, `"|"` (a gap that takes all the free space), or a table. One of `module`, `text`, `spacer` or `stretch` says what it is; the others place it.
---@class StatusbarLayoutItem
---@field module? StatusbarModule The module to show.
---@field text? string A fixed piece of text, e.g. a separator.
---@field spacer? number A gap of this many pixels.
---@field stretch? number Share of the free space this item takes (default 0: its natural width). A gap with a stretch pushes what follows away from what precedes.
---@field min_width? integer Narrowest width in pixels.
---@field max_width? integer Widest width in pixels.
---@field margin? integer|integer[] Space around it: a number, or `{left, right}`.
---@field align? "left"|"center"|"right" Where it sits inside its slot when the slot is wider.
---@field at? number Place it at this position along the bar (0 to 1) instead of in the flow.
---@field anchor? "left"|"center"|"right" With `at`: which edge of the item is put at that position (default left).

---@class OptStatusbar
---@field layout? (StatusbarModule|"|"|StatusbarLayoutItem)[]|(StatusbarModule|"|"|StatusbarLayoutItem)[][] Which modules the statusbar shows and where: one list is one row, a list of lists is several rows. Items are placed left to right and the free space goes to the gaps. Example: `{"session", "filename", "|", {module = "page", at = 0.5, anchor = "center"}, "zoom", "progress", "mode"}`.
---@field padding? integer|integer[] Padding in pixels: one number for all four sides, or a four-element array `{left, top, right, bottom}`. Reading it gives the array.
---@field visible? boolean Whether the status bar is shown.
---@field components? OptStatusbarComponents Per-component visibility toggles.
lektra.opt.statusbar = {
    ---@type OptStatusbarComponents
    components = {
        ---@type OptStatusbarComponentMode
        mode = {},
        ---@type OptStatusbarComponentPageNumber
        pagenumber = {},
        ---@type OptStatusbarComponentSession
        session = {},
        ---@type OptStatusbarComponentZoom
        zoom = {},
        ---@type OptStatusbarComponentFileName
        filename = {},
        ---@type OptStatusbarComponentProgress
        progress = {},
    },
}

---@class OptZoom
---@field anchor_to_mouse? boolean Zoom anchored to the mouse cursor position.
---@field factor? number Zoom step multiplier applied per scroll tick.
---@field level? number Current zoom level (1.0 = 100%).
lektra.opt.zoom = {}

---@class OptSelection
---@field color? Color Text selection highlight color (`Color`).
---@field copy_on_select? boolean Automatically copy selected text to clipboard.
---@field drag_threshold? integer Pixel distance before a drag is recognized.
lektra.opt.selection = {}

---@class OptSplit
---@field dim_inactive? boolean Dim the inactive split pane.
---@field dim_inactive_opacity? number Opacity of the inactive pane when dimmed (0.0–1.0).
---@field focus_border? boolean Draw a border around the currently focused split.
---@field focus_border_color? Color Border color as an RRGGBBAA hex integer (e.g. 0xFF4FC3F7).
---@field focus_border_width? integer Border thickness in pixels.
---@field focus_follows_mouse? boolean Focus a split pane when the mouse enters it.
---@field maximize_indicator? boolean Show a badge in the corner of the maximized split.
---@field maximize_indicator_color? Color Badge color as an RRGGBBAA hex integer (e.g. 0xCC2979FF).
---@field mouse_follows_focus? boolean Warp the mouse to the focused pane.
lektra.opt.split = {}

---@class OptScrollbars
---@field auto_hide? boolean Automatically hide scrollbars when idle.
---@field hide_timeout? number Seconds of inactivity before scrollbars hide.
---@field horizontal? boolean Show the horizontal scrollbar.
---@field search_hits? boolean Show search hit markers on the scrollbar track.
---@field size? integer Scrollbar track width in pixels.
---@field vertical? boolean Show the vertical scrollbar.
lektra.opt.scrollbars = {}

---@class OptJumpMarker
---@field color? Color Jump marker color (`Color`).
---@field enabled? boolean Whether jump markers are shown.
---@field fade_duration? number Duration in seconds for the fade-out animation.
lektra.opt.jump_marker = {}

---@class OptLinks
---@field boundary? boolean Draw a boundary box around detected links.
---@field detect_urls? boolean Detect plain-text URLs as clickable links.
---@field enabled? boolean Whether link detection and navigation is active.
---@field hover_preview? boolean Hovering an internal link shows a preview of its target, instead of the tooltip.
---@field hover_preview_delay? integer Milliseconds to hover before the preview shows.
---@field hover_preview_height? integer Height of the hover preview in pixels.
---@field hover_preview_width? integer Width of the hover preview in pixels.
---@field url_regex? string Regular expression used to detect URLs.
lektra.opt.links = {}

---@class OptLinkHints
---@field bg? Color Link hint background color (`Color`).
---@field fg? Color Link hint foreground/text color (`Color`).
---@field size? number Font size for link hint labels.
lektra.opt.link_hints = {}

---@alias TabsElideMode
---| '"right"'
---| '"left"'
---| '"middle"'
---| '"none"'

---@alias TabsLocation
---| '"top"'
---| '"bottom"'
---| '"left"'
---| '"right"'

---@alias TabsOpenPosition
---| '"end"'           # Append after all existing tabs (default).
---| '"start"'         # Insert at index 0.
---| '"after_current"' # Insert immediately after the current tab.

---@alias TabsCloseButtonMode
---| '"all"'     # Every tab shows its own close button (default).
---| '"current"' # Only the active tab shows a close button.
---| '"hidden"'  # No close buttons at all.

---@class OptTabs
---@field auto_hide? boolean Hide the tab bar when only one tab is open.
---@field close_button_mode? TabsCloseButtonMode When to show the tab close (x) button.
---@field elide_mode? TabsElideMode Where to elide long tab titles.
---@field full_path? boolean Display the full file path as the tab title.
---@field lazy_load? boolean Defer loading of background tabs until they are activated.
---@field location? TabsLocation Where the tab bar sits in the main window.
---@field max_width? integer Widest a tab may be, in pixels along the tab bar (0: no limit).
---@field min_width? integer Narrowest a tab may be, in pixels along the tab bar (0: no limit).
---@field movable? boolean Allow tabs to be dragged and reordered.
---@field open_position? TabsOpenPosition Where a newly opened tab is placed in the tab bar.
---@field scroll_text_on_hover? boolean Scroll a tab title that does not fit while the mouse is over the tab (tabs at the top or bottom).
---@field visible? boolean Whether the tab bar is shown.
lektra.opt.tabs = {}

---@class OptPickerShadow
---@field blur_radius? integer Shadow blur radius in pixels.
---@field enabled? boolean Draw a drop shadow behind the picker.
---@field offset_x? integer Horizontal shadow offset in pixels.
---@field offset_y? integer Vertical shadow offset in pixels.
---@field opacity? integer Shadow opacity (0–255).

---@alias PickerKeys string|string[] One key (`"Up"`) or a list of keys (`{"Up", "Ctrl+P"}`).

---@class OptPickerKeys
---@field accept? PickerKeys Accept the selected row.
---@field collapse? PickerKeys Collapse the selected row.
---@field dismiss? PickerKeys Close the picker.
---@field down? PickerKeys Move the selection down.
---@field expand? PickerKeys Expand the selected row.
---@field history_next? PickerKeys Next search in the picker history.
---@field history_prev? PickerKeys Previous search in the picker history.
---@field page_down? PickerKeys Move the selection a page down.
---@field page_up? PickerKeys Move the selection a page up.
---@field section_next? PickerKeys Jump to the next section.
---@field section_prev? PickerKeys Jump to the previous section.
---@field toggle_structure_mode? PickerKeys Switch between the tree and the flat list.
---@field up? PickerKeys Move the selection up.

---@class OptPicker
---@field width? number Picker widget width (as a fraction of the window or in pixels).
---@field height? number Picker widget height.
---@field border? boolean Draw a border around the picker.
---@field alternating_row_color? boolean Alternate row background colors in the picker list.
---@field prompt? string Placeholder text shown in the picker's search box.
---@field shadow? OptPickerShadow Drop-shadow sub-table.
---@field keys? OptPickerKeys Keys used in every picker (same actions as `[picker.keys]` in config.toml).
---@field highlight_matches? boolean Highlight the typed text in matching picker rows.
---@field highlight_matches_color? string|integer Color of the match highlight (0xRRGGBBAA or "#RRGGBBAA").
lektra.opt.picker = {
    ---@type OptPickerShadow
    shadow = {},
    ---@type OptPickerKeys
    keys = {},
}

---@class OptOutline
---@field highlight_matches? boolean Highlight the typed text in matching rows.
---@field highlight_matches_color? string|integer Color of the match highlight (0xRRGGBBAA or "#RRGGBBAA").
---@field flat_menu? boolean Display the outline as a flat list instead of a tree.
---@field generate_heading_ratio? number Minimum font-size ratio (vs body text) for a line to be treated as a heading by `generate_outline`.
---@field generate_max_levels? integer Maximum number of heading tiers `generate_outline` will produce.
---@field indent_width? integer Pixels to indent each outline level.
---@field preload? boolean Load the outline in the background after a document opens (true), or the first time the outline picker is opened (false).
---@field prompt? string Placeholder text shown in the outline picker's search box.
---@field show_page_number? boolean Show the page number next to each outline entry.
lektra.opt.outline = {}

---@class OptHighlightSearch
---@field highlight_matches? boolean Highlight the typed text in matching rows.
---@field highlight_matches_color? string|integer Color of the match highlight (0xRRGGBBAA or "#RRGGBBAA").
---@field flat_menu? boolean Display search results as a flat list.
---@field prompt? string Placeholder text shown in the highlight-search picker's search box.
lektra.opt.highlight_search = {}

---@class OptCommandPalette
---@field highlight_matches? boolean Highlight the typed text in matching rows.
---@field highlight_matches_color? string|integer Color of the match highlight (0xRRGGBBAA or "#RRGGBBAA").
---@field prompt? string Placeholder text shown in the command palette input.
---@field vscrollbar? boolean Show a vertical scrollbar in the command palette.
---@field show_shortcuts? boolean Show keyboard shortcuts next to commands.
---@field description? boolean Show command descriptions in the palette.
---@field sort_by_frequency? boolean Sort commands by how often you've picked them from the palette (most-used first, smex-style). Usage counts persist across sessions when persist_frequency is true.
---@field persist_frequency? boolean Persist command-palette usage counts to disk so sort_by_frequency ranking survives across restarts. When false, usage is still tracked for the current session but nothing is loaded/saved to disk.
lektra.opt.command_palette = {}

---@class OptLLMView
---@field api_key? string API key for the LLM endpoint (empty for local models).
---@field api_url? string Chat-completions URL of the LLM endpoint.
---@field auto_run? boolean Run the Lua the assistant writes as soon as its reply is complete, without pressing Run.
---@field dock_area? "left"|"right"|"top"|"bottom" Where the panel is docked.
---@field extra_body? table Extra fields merged into every request body.
---@field font_size? number Font size in points of the text in the panel; 0 uses the application's font size.
---@field model? string Model name sent with each request.
---@field save_history? boolean Save chats to disk so they can be reopened from the History menu.
---@field separate_window? boolean Show the panel in a separate window.
---@field show_at_startup? boolean Open the LLM panel when Lektra starts.
---@field tools? boolean Let the assistant use tools (run_command, run_lua, lookup_api) and look up the Lua API on demand instead of receiving all of it with every request. Turn off for models without tool calling.
lektra.opt.llm_view = {}

---@class OptLaserPointer
---@field enabled? boolean Use the laser pointer cursor when entering presentation mode.
---@field color? string|integer Laser pointer color (0xRRGGBBAA or "#RRGGBBAA").
---@field size? integer Laser pointer size in pixels.

---@class OptPresentation
---@field laser_pointer? OptLaserPointer Laser pointer shown as the cursor in presentation mode.

---@class OptReflow
---@field font_family? string Font family for reflowable documents (installed font, serif, sans-serif or monospace); empty keeps the document's own.
---@field font_size? number Font size in points.
---@field line_spacing? number Line spacing as a multiple of the font size; 0 keeps the document's own.
lektra.opt.reflow = {}

---@class OptRendering
---@field antialiasing? boolean Enable antialiasing for page rendering.
---@field antialiasing_bits? integer Number of multisampling bits for antialiasing (e.g. 4, 8).
---@field text_antialiasing? boolean Enable text-specific antialiasing.
---@field smooth_pixmap_transform? boolean Use smooth (bilinear) scaling for pixmaps.
---@field backend? Backend Rendering backend
---@field dpr? number|table<string, number> Device pixel ratio: a single number for every screen, or a table keyed by screen name (e.g. `{ ["eDP-1"] = 1.5, ["HDMI-A-1"] = 1.0 }`).
lektra.opt.rendering = {}

---@class OptBehavior
---@field auto_reload? boolean Automatically reload the document when the file changes on disk.
---@field auto_scroll? boolean Scroll to keep an out-of-view text selection visible.
---@field cache_pages? integer Number of rendered pages to keep in the page cache.
---@field cache_password? boolean Keep the password for encrypted PDFs in memory so auto-reload can re-authenticate.
---@field close_on_last_tab? boolean Quit LEKTRA when the last tab is closed (confirm_on_quit still applies).
---@field confirm_on_quit? boolean Show a confirmation dialog before quitting.
---@field dont_invert_images? boolean Exclude images from colour inversion and high-contrast stretch.
---@field high_contrast? boolean High-contrast tone stretch applied after tint / invert (accessibility).
---@field high_contrast_black_point? integer Pixel value ≤ this becomes pure black (0-255).
---@field high_contrast_white_point? integer Pixel value ≥ this becomes pure white (0-255).
---@field invert_mode? boolean Start with colour inversion enabled.
---@field mupdf_store_size? integer Maximum size of MuPDF's internal decoded-image / glyph store, in MB.
---@field num_recent_files? integer Maximum number of recent files to remember.
---@field open_last_visited? boolean Reopen the last visited document on startup.
---@field page_history_limit? integer Maximum number of entries in the page-navigation history (parsed from TOML key `page_history`).
---@field preload_pages? integer Number of pages to pre-render ahead of the current page.
---@field recent_files? boolean Track recently opened files.
---@field remember_last_visited? boolean Remember and restore the last visited page.
---@field single_instance? boolean Enforce a single application instance.
---@field undo_limit? integer Maximum number of undo steps.
lektra.opt.behavior = {}

---@class OptPreviewSizeRatio
---@field width? number Width of the preview as a fraction of the window width.
---@field height? number Height of the preview as a fraction of the window height.

---@class OptPreview
---@field size_ratio? OptPreviewSizeRatio Table with `width` and `height` ratio fields.
---@field border_radius? integer Corner radius of the preview popup in pixels.
---@field close_on_click_outside? boolean Close the preview when clicking outside it.
---@field opacity? number Opacity of the preview popup (0.0–1.0).
lektra.opt.preview = {}

---@class OptUpdates
---@field check? boolean Check once a day, in the background, whether a newer release is available. The check_for_updates command works whatever this is.
---@field whats_new? boolean After an update, show a banner that opens the list of changes.
lektra.opt.updates = {}

---@class OptDonate
---@field reminders? boolean Now and then show a small banner saying that Lektra can be supported (rarely, and "Don't ask again" turns it off).
lektra.opt.donate = {}

---@class OptMisc
---@field color_dialog_colors? string[] Preset colours shown in the colour picker (e.g. `"#FF112233"`).
lektra.opt.misc = {}

-- ###########################################################
-- The options of one view (`View:opt()`)

--- The sections of the options that a view has of its own: the ones that make
--- sense for a single view. The others (tabs, statusbar, window, pickers, ...)
--- belong to the application and only exist in `lektra.opt`.
---@class ViewOptions
---@field page OptPage
---@field search OptSearch
---@field annotations OptAnnotations
---@field layout OptLayout
---@field reflow OptReflow
---@field zoom OptZoom
---@field selection OptSelection
---@field split OptSplit
---@field scrollbars OptScrollbars
---@field jump_marker OptJumpMarker
---@field links OptLinks
---@field link_hints OptLinkHints
---@field rendering OptRendering
---@field behavior OptBehavior

-- ###########################################################
-- The global defaults only (`lektra.opt_global`)

--- Every section of the options, as in `lektra.opt`. Writing one here changes the
--- default for views created later, and not the current view (Vim's `:setglobal`;
--- `lektra.opt` is `:set`, and `View:opt()` is `:setlocal`).
---@class GlobalOptions
---@field page? OptPage
---@field synctex? OptSynctex
---@field search? OptSearch
---@field annotations? OptAnnotations
---@field thumbnail_panel? OptThumbnailPanel
---@field portal? OptPortal
---@field window? OptWindow
---@field layout? OptLayout
---@field statusbar? OptStatusbar
---@field reflow? OptReflow
---@field zoom? OptZoom
---@field selection? OptSelection
---@field split? OptSplit
---@field scrollbars? OptScrollbars
---@field jump_marker? OptJumpMarker
---@field links? OptLinks
---@field link_hints? OptLinkHints
---@field tabs? OptTabs
---@field picker? OptPicker
---@field outline? OptOutline
---@field highlight_search? OptHighlightSearch
---@field command_palette? OptCommandPalette
---@field rendering? OptRendering
---@field behavior? OptBehavior
---@field preview? OptPreview
---@field updates? OptUpdates
---@field donate? OptDonate
---@field misc? OptMisc
---@field llm_view? OptLLMView
---@field presentation? OptPresentation

---@type GlobalOptions
lektra.opt_global = {}
