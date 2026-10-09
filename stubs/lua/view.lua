---@meta
lektra = lektra or {}
lektra.view = {}

---@enum Mode
---Represents the different interaction modes available in Lektra.
Mode = {
    None = 0,
    VisualLine = 1,
    RegionSelection = 2,
    TextSelection = 3,
    TextHighlight = 4,
    AnnotSelect = 5,
    AnnotRect = 6,
    AnnotPopup = 7,
    TextUnderline = 8,
}

---@class Container
---The splits of a tab. Get one with `View:container()`.
local Container = {}

---Splits the focused view, putting a new view beside it (with a vertical divider, like the `split_vertical` command). The new view shows the same document unless a file is given.
---@param file? string Document to open in the new view.
---@return View? new_view The new view; nil if the split could not be made.
function Container:vsplit(file) end

---Splits the focused view, putting a new view below it (like the `split_horizontal` command).
---@param file? string Document to open in the new view.
---@return View? new_view The new view; nil if the split could not be made.
function Container:hsplit(file) end

---All views (splits) in the tab.
---@return View[] views
function Container:views() end

---Number of splits in the tab.
---@return integer count
function Container:view_count() end

---The focused view.
---@return View? view
function Container:view() end

---Moves focus to a split.
---@param target View|"left"|"right"|"up"|"down" A view, or a direction from the focused split.
function Container:focus(target) end

---Closes a split. The last remaining split is never closed.
---@param view? View Split to close (default: the focused one).
---@return boolean closed
function Container:close_view(view) end

---Closes every split except one.
---@param view? View Split to keep (default: the focused one).
function Container:close_others(view) end

---Maximizes the focused split, or restores it if it is already maximized.
function Container:toggle_maximize() end

---@return boolean maximized
function Container:is_maximized() end

---@class View
---Represents a View in Lektra.
local View = {}

---@class Location
---Represents a location in a document.
---@field page integer The page number.
---@field x number The x-coordinate on the page.
---@field y number The y-coordinate on the page.
local Location = {}

---@class OpenFileOptions
---Options for opening a file in a view.
---@field fit string? The fit mode to use when opening the document (e.g., "width", "height", "window").
---@field zoom number? The zoom level to use when opening the document (e.g., 1.0 for 100%).
local OpenFileOptions = {}

---@class SearchOptions
---Options for searching in a document.
---@field case_sensitive boolean? Whether the search should be case sensitive (default: false).
---@field whole_word boolean? Whether to match whole words only (default: false).
---@field regex boolean? Whether the query should be treated as a regular expression (default: false
local SearchOptions = {}

-- ##########################################

---Returns the current page number.
---@return integer pageno
function View:pageno() end

---Opens a document in the view.
---@overload fun(arg: { file: string, opts: OpenFileOptions }) Opens a document using an argument table with file and opts fields.
---@param file string The path to the document to open.
---@param opts? OpenFileOptions Optional parameters for opening the document (e.g., { fit = "width", zoom = 1.5 }).
function View:open(file, opts) end

---Close the document.
function View:close() end

---Goes to the specified page number.
---@param pageno integer
function View:goto_page(pageno) end

---Returns the total number of pages in the document.
---@return integer pagecount
function View:page_count() end

---Goto the specified location in the document.
---@param location Location
function View:goto_location(location) end

---Returns the current location in the document.
---@return Location location
function View:location() end

---Goes back to the previous location in the document history.
function View:history_back() end

---Goes forward to the next location in the document history.
function View:history_forward() end

---Returns the current zoom level of the document.
---@return number zoom
function View:zoom() end

---[Returns a table of properties for the document in the view. The properties may include
---metadata such as title, author, creation date, etc., depending on the file type and what
---information is available.]
---@return table properties A table containing the document properties.
function View:properties() end

---Sets the zoom level of the document.
---@param zoom number The desired zoom level (e.g., 1.0 for 100%).
function View:set_zoom(zoom) end

---Returns the current fit mode of the document.
---@return string mode The current fit mode (e.g., "width", "height", "window").
function View:fit() end

---Sets the fit mode of the document.
---@param mode string The fit mode (e.g., "width", "height", "window").
function View:set_fit(mode) end

---Returns the current rotation of the document in degrees.
---@return integer rotation The current rotation (e.g., 0, 90, 180, 270).
function View:rotation() end

---Sets the rotation of the document.
---@param rotation integer The desired rotation in degrees (e.g., 0, 90, 180, 270).
function View:set_rotation(rotation) end

---Returns the layout mode of the document.
---@return string layout The current layout mode (e.g., "single", "book", "horizontal", "vertical").
function View:layout() end

---Sets the layout mode of the document.
---@param layout string The desired layout mode (e.g., "single", "book", "horizontal", "vertical").
function View:set_layout(layout) end

---Returns true if there is a selection in the document, false otherwise.
---@return boolean has_selection
function View:has_selection() end

---Returns the text of the current selection in the document.
---@param formatted? boolean Whether to return the selection text with formatting (e.g., newlines, tabs) or as plain text. Default is false (plain text).
---@param page_separator? string An optional string to use as a separator between pages in the selection text when formatted is true. Default is "\n--- Page Break ---\n".
---@return string selection_text
function View:selection_text(formatted, page_separator) end

---Clears the current selection in the document.
function View:clear_selection() end

---Searches for the given query in the document and highlights the results.
---@overload fun(arg: { query: string, regex: boolean }) Searches for the query as plain text (default).
---@param query string The search query to find in the document.
---@param regex? boolean Optional parameter indicating whether the query should be treated as a regular expression (default: false).
function View:search(query, regex) end

---Finds the next occurrence of the last search query and highlights it.
function View:search_hit_next() end

---Finds the previous occurrence of the last search query and highlights it.
function View:search_hit_previous() end

---Cancels the current search and clears any search highlights.
function View:search_cancel() end

---Returns a table of search hits for the last search query. Each hit is represented as a Location object.
---@return Location[] hits
function View:search_hits() end

---Returns the total number of search hits for the last search query.
---@return integer hit_count
function View:search_hit_count() end

---Returns the file path of the currently opened document in the view.
---@return string file_path
function View:file_path() end

---Returns the file type of the currently opened document in the view (e.g., "pdf", "epub").
---@return string file_type
function View:file_type() end

---Registers a callback function for the specified event on the view.
---@overload fun(event: { name: string, callback: function }) Registers a callback using an event table with name and callback fields.
---@param event string The name of the event to listen for (e.g., "page_changed", "selection_changed").
---@param callback function The callback function to be called when the event occurs. The callback function will receive the view instance and any relevant event data as arguments.
---@return integer handle A unique handle that can be used to unregister the callback later.
function View:register(event, callback) end

---Unregisters a callback function for the specified event on the view.
---@overload fun(event: { name: string, callback: function, handle: integer}) Unregisters a callback using an event table with name and callback fields.
---@param event string The name of the event to stop listening for (e.g., "page_changed", "selection_changed").
---@param handle integer The unique handle returned by the `on` method when the callback was registered.
function View:unregister(event, handle) end

---Registers a one-time callback function for the specified event on the view. The callback will be automatically unregistered after it is called once.
---@overload fun(event: { name: string, callback: function }) Registers a one-time callback using an event table with name and callback fields.
---@param event string The name of the event to listen for (e.g., "page_changed", "selection_changed").
---@param callback function The callback function to be called when the event occurs. The callback function will receive the view instance and any relevant event data as arguments.
function View:once(event, callback) end

---Registers a callback to customize the context menu for selections.
---@param menu_type string "TextSelection" or "RegionSelection"
---@param callback function Callback invoked with (view, menu)
---@return integer handle
function View:register_context_menu(menu_type, callback) end

---Unregisters a context menu callback.
---@param menu_type string "TextSelection" or "RegionSelection"
---@param handle integer
function View:unregister_context_menu(menu_type, handle) end

---Returns true if the document in the view has unsaved changes, false otherwise.
function View:is_modified() end

---Saves the current document. If the document has unsaved changes, it will be saved to its current file path.
function View:save() end

---Saves the current document to a new file path. If the document has unsaved changes, it will be saved to the specified file path.
---@param file_path string The file path to save the document to.
function View:save_as(file_path) end

---Returns the text of the current page. This may return an empty string for certain file types (e.g., images) or if the page does not contain extractable text. Use `View:page_text()` for another page.
---@param formatted boolean Whether to return the text content with formatting (e.g., newlines, tabs) or as plain text. Default is false (plain text).
---@return string text_content
function View:extract_text(formatted) end

---Saves pages to files, as pictures or in other formats. The format is the extension of the file: png (the default when there is none), jpg, webp, bmp, tif ... make one picture per page; pdf, txt, html, xhtml, cbz, docx and odt make one file with all the pages; svg makes one file per page. Images and DjVu can only be written as pictures or pdf. Nothing is written if anything is wrong, and an existing file is not replaced unless `overwrite` is true.
---
---With several pictures, one name is a pattern: `%d` (or `%03d`, padded) is replaced by the page number, and a name without it gets `-<number>` before its extension (`out.png` gives `out-2.png`, `out-3.png`). A list of names gives one name per page instead. A single page keeps the name as it is.
---@param names string|string[] The file name, or a list of names (one per page).
---@param pages? integer|string|integer[] Which pages (1-based): a number, a list of numbers, or text such as `"1-5,8"`, `"10-"`, `"all"`, `"odd"`, `"even"`, `"current"`, `"last"`. Default: the current page.
---@param opts? {dpi?: integer, overwrite?: boolean, split?: boolean} `dpi` is the resolution of pictures (10 to 1200, default 150); a picture document is saved with its own pixels. With `split` the formats that make one file for all the pages (pdf, txt, html, ...) make one file per page instead, named like pictures.
---@return string[]? files The files that were written, or nil if it failed.
---@return string? err Why it failed.
function View:export_pages(names, pages, opts) end

---Returns the text of a page, "" if it has none (e.g. a scanned page or an image).
---@param pageno? integer Page number, 1-based (default: the current page).
---@param formatted? boolean Keep the layout (newlines, tabs) instead of plain text. Default false.
---@return string? text nil if the page does not exist.
function View:page_text(pageno, formatted) end

---Returns the unique identifier of the view.
---@return integer id
function View:id() end

---Returns the spacing between pages in the document view, in pixels.
---@return integer spacing
function View:spacing() end

---Gets the current mode of the view
---@return Mode mode The current interaction mode (e.g., "select", "pan", "zoom").
function View:mode() end

---Sets the interaction mode of the view
---@param mode Mode The desired interaction mode
function View:set_mode(mode) end

---Sets the device pixel ratio (DPR) for the view
---@param dpr number The desired device pixel ratio (e.g., 1.0 for standard displays, 2.0 for high-DPI displays). Setting the DPR can improve rendering quality on high-DPI screens.
function View:set_dpr(dpr) end

---Puts the view into region-selection mode; once the user drags out a rubber-band and releases, `callback` is called once with the selected area and the interaction mode reverts to normal (no context menu is shown for this one selection).
---@param callback fun(area: { x: number, y: number, w: number, h: number }) Callback invoked with the selected region in scene coordinates.
function View:region_select(callback) end

---This view's local options, a copy of the global options taken when the view was created (a split copies the view it was split from). Writing to it changes only this view and applies immediately; `lektra.opt` sets the global default and the current view. Has the same per-view sections as `lektra.opt` (page, search, annotations, layout, reflow, zoom, selection, split, scrollbars, jump_marker, links, link_hints, rendering, behavior), not app-wide ones like tabs or statusbar. Using a table after its view is closed raises an error.
---@return ViewOptions
function View:opt() end

---Sets the local options of this view back to what a new view of its file type would start with: the global options, and the `[filetype.<type>]` overrides on top. Like Vim's `:setlocal opt<`.
---@param section? string One section (`"page"`, `"layout"`, `"zoom"`, ... as in `View:opt()`); without one, all of them.
---@return boolean ok
function View:opt_reset(section) end

---Same interaction as `region_select`, but `callback` is passed the selected region rendered as a base64-encoded PNG string instead of the rect — handy for feeding a screenshot region to an OCR/vision-model API or saving it out via `io.open` plus a base64 decoder. The string is empty if the dragged region didn't land on a rendered page.
---@param callback fun(image_base64: string) Callback invoked with the selected region as a base64-encoded PNG string.
function View:region_select_image(callback) end

---Gets the current device pixel ratio (DPR) for the view
---@return number dpr The current device pixel ratio (e.g., 1.0 for standard displays, 2.0 for high-DPI displays).
function View:dpr() end

---Sets whether to invert the colors of the document in the view. Inverting colors can be useful for reducing eye strain or improving readability in low-light conditions.
---@param invert boolean True to enable color inversion, false to disable it.
function View:set_invert(invert) end

---Gets whether color inversion is currently enabled for the document in the view.
---@return boolean invert True if color inversion is enabled, false otherwise.
function View:is_invert() end

---Returns if the document has unsaved changes.
function View:is_modified() end

---Sets the layout mode of the document in the view. The layout mode determines how pages are arranged in the view (e.g., single page, book view, horizontal scrolling, vertical scrolling).
---@param layout LayoutMode The desired layout mode
function View:set_layout(layout) end

---Gets the current layout mode of the document in the view. The layout mode determines how pages are arranged in the view (e.g., single page, book view, horizontal scrolling, vertical scrolling).
---@return LayoutMode layout The current layout mode
function View:layout() end

---Returns the portal associated with the view, if any.
---@return View portal The portal view associated with the current view, or nil if there is no portal.
function View:portal() end

---Returns true if the view is a portal, false otherwise.
---@return boolean is_portal True if the view is a portal, false otherwise.
function View:is_portal() end

---Sets the portal for the view. A portal is a secondary view that can be used to display a different document or a different part of the same document. When a portal is set for a view, the portal's content will be displayed alongside the main view, allowing for side-by-side comparison or reference.
---@param portal_view View The view to set as the portal for the current view. Setting a
function View:set_portal(portal_view) end

---Sets the active state of the view. An active view is the one that currently has focus and receives user input. Setting a view as active will bring it to the foreground and allow the user to interact with it.
---@param active boolean True to set the view as active, false to deactivate it.
function View:set_active(active) end

---Returns true if the view is currently active, false otherwise. An active view is the one that currently has focus and receives user input.
---@return boolean is_active True if the view is active, false otherwise.
function View:is_active() end

---Enables or disables visual line mode for the view. Visual line mode is a display mode that wraps lines of text at the edge of the view, allowing for easier reading of long lines without horizontal scrolling. When visual line mode is enabled, lines will be wrapped based on the width of the view, and the user can navigate through the wrapped lines as if they were separate lines.
---@param enabled boolean True to enable visual line mode, false to disable it.
function View:set_visual_line_mode(enabled) end

---Returns true if visual line mode is currently enabled for the view, false otherwise. Visual line mode is a display mode that wraps lines of text at the edge of the view, allowing for easier reading of long lines without horizontal scrolling.
---@return boolean enabled True if visual line mode is enabled, false otherwise.
function View:is_visual_line_mode() end

---Returns true if the view is currently in thumbnail view mode, false otherwise. Thumbnail view mode is a display mode that shows small thumbnail images of each page in the document, allowing for quick navigation and overview of the document's structure.
---@return boolean is_thumbnail True if the view is in thumbnail view mode, false otherwise.
function View:is_thumbnail_view() end

---Returns the Container object associated with the view.
---@return Container container The container object associated with the view, which provides access to the underlying document and rendering context.
function View:container() end

---Returns the outline of the document (if it exists)
---@return string[] table
function View:outline() end

---@class HighlightEntry
---Represents a single highlight annotation extracted from a document.
---@field page integer The 1-based page number where the highlight appears.
---@field text string The highlighted text content.
---@field comment string? An optional note attached to the highlight, or nil if none.

---Exports all highlight annotations in the document to a JSON file.
---Each entry in the JSON array contains `page` (1-based), `text`, and optionally `comment`.
---Returns true on success, or nil on failure.
---@param path string The file path to write the JSON output to.
---@return true | nil result `true` on success, `nil` on failure.
function View:export_highlights(path) end

---@class Annotation
---@field id integer Identifies the annotation on its page. It changes when an undo brings the annotation back.
---@field page integer 1-based page number.
---@field type "highlight"|"rect"|"note"|"other"
---@field x number Position on the page, in points, from the top left.
---@field y number
---@field w number
---@field h number
---@field color string "#rrggbb"
---@field opacity number 0 to 1.
---@field comment string
---@field text? string Highlights: the text under it.
---@field rects? {x: number, y: number, w: number, h: number}[] Highlights: one per line.

---@class HistoryLocation
---@field pageno integer 1-based page number.
---@field x number
---@field y number

---Returns the jump locations of the view, in the order they were made: what `history_back` and `history_forward` walk through. The second value is the position of the current one in the list (0 if there is none).
---@return HistoryLocation[] locations
---@return integer current
function View:history_stack() end

---Marks the current location of the view with a name. A mark named with a lower-case letter (a-z) belongs to this view, an upper-case one (A-Z) is global. The same as the `mark_set` command.
---@param char string
---@return boolean ok
function View:set_mark(char) end

---Goes to a mark (and records the jump in the history). A global mark can switch to another view. The same as the `mark_goto` command.
---@param char string
---@return boolean found False if there is no such mark.
function View:goto_mark(char) end

---@class ImageMetadata
---@field width integer Pixels.
---@field height integer Pixels.
---@field format string "png", "jpeg", ...
---@field animated boolean
---@field frames integer Frames of an animation, or pages of a multi-page TIFF.
---@field dpi_x? number Only if the file stores it.
---@field dpi_y? number
---@field exif table<string,string> EXIF tags by name (empty if there are none).

---Returns what is known about the image the view shows: its size, format, resolution and EXIF tags. nil and a message if the document is not an image.
---@return ImageMetadata? metadata
---@return string? error
function View:image_metadata() end

---Converts a point of the canvas (the scene: where pages are laid out, in pixels) to a point of a page, in page points from the top left of the page, like `page_size` and the annotations. nil if the point is not on a page.
---@param x number
---@param y number
---@return integer? pageno 1-based.
---@return number? page_x
---@return number? page_y
function View:scene_to_page(x, y) end

---The opposite of `scene_to_page`: where a point of a page (in page points from the top left) is on the canvas. It works for the pages that are loaded, that is the visible ones and their neighbours; nil and a message for another page.
---@param pageno integer 1-based.
---@param x number
---@param y number
---@return number? scene_x
---@return number? scene_y
---@return string? error
function View:page_to_scene(pageno, x, y) end

---Moves this split into a window of its own, at the page it shows (the zoom and position are not kept). The view is closed in this window. False if it is the only split of its tab, has no file, or the window could not be started.
---@return boolean moved
function View:detach_to_window() end

---Moves this split into a tab of its own, at the page it shows. False if it is the only split of its tab or has no file.
---@return boolean moved
function View:detach_to_tab() end

---Returns the annotations of a page (1-based), or of the whole document without `page`. PDF only.
---@param page? integer
---@return Annotation[]? annotations
---@return string? error
function View:annotations(page) end

---Highlights one or several rectangles (one per line of text) on a page, in points from the top left. Can be undone.
---@param page integer
---@param rects {x: number, y: number, w: number, h: number}|number[]|({x: number, y: number, w: number, h: number}|number[])[] A rectangle, or a list of them. A rectangle is `{ x =, y =, w =, h = }` or `{ x, y, w, h }`.
---@param opts? { color?: string, comment?: string } `color` is "#rrggbb" or a colour name.
---@return integer? id
---@return string? error
function View:add_highlight(page, rects, opts) end

---Adds a sticky note at a position (points from the top left). Can be undone.
---@param page integer
---@param x number
---@param y number
---@param text string
---@return integer? id
---@return string? error
function View:add_note(page, x, y, text) end

---Adds a rectangle. Can be undone.
---@param page integer
---@param rect {x: number, y: number, w: number, h: number}|number[]
---@param opts? { comment?: string }
---@return integer? id
---@return string? error
function View:add_rect(page, rect, opts) end

---Changes the comment and/or colour of an annotation. Cannot be undone.
---@param page integer
---@param id integer
---@param opts { comment?: string, color?: string }
---@return boolean found False if there is no such annotation.
function View:set_annotation(page, id, opts) end

---Removes an annotation. Can be undone.
---@param page integer
---@param id integer
---@return boolean found False if there is no such annotation.
function View:remove_annotation(page, id) end

-- ##########################################


---Gets the view with the given id.
---@param id? integer
---@return View view
lektra.view.get = function(id) end

---Returns the current View object for the active document in the current tab.
---`Note` If there are multiple documents open in the current tab, it will return the active document's view. If no documents are open, it will return nil.
---@return View view
lektra.view.current = function() end

---Returns a table of View objects for the current tab.
---@param tabindex? integer The tab index (1-based) to get the views of.
---
---`Note` If not provided, it will return all documents in the current tab.
---@return View[] documents
lektra.view.list = function(tabindex) end

-- ##########################################
-- Links, selection by position, scrolling and page geometry.
-- Pages are 1-based. Page coordinates (x, y, rect) are in points on the
-- unrotated page with the origin at its top-left.

---@class PageRect
---@field x0 number
---@field y0 number
---@field x1 number
---@field y1 number

---A link on a page, as returned by `View:links()`.
---@class PageLink
---@field page integer Page the link is on.
---@field index integer Position in the page's link list.
---@field rect PageRect Where the link is on its page.
---@field uri string The link's address.
---@field type "page"|"section"|"fit_v"|"fit_h"|"location"|"external"
---@field target_page integer? Page an internal link goes to.
---@field target_x number? Position on the target page.
---@field target_y number? Position on the target page.

---A position in the document.
---@class PagePosition
---@field page integer
---@field x number
---@field y number

---Lists the links on a page. Loads the page if it was never shown, so it can
---take a moment. Empty for formats without links.
---@param pageno? integer Page (default: current page).
---@return PageLink[] links
function View:links(pageno) end

---Follows a link, as if it had been clicked (jumps, or opens an external address).
---@overload fun(self: View, pageno: integer, index: integer): boolean
---@param link PageLink A link from `View:links()`.
---@return boolean ok False if the link doesn't exist.
function View:follow_link(link) end

---Starts link hint mode in the current tab, like the `link_hint_visit` and
---`link_hint_copy` commands. The view must be the current tab.
---@param mode? "visit"|"copy" "visit" follows the chosen link (default); "copy" copies its address.
---@return boolean started
function View:link_hints(mode) end

---Selects the text between two positions, in reading order. Both pages must be
---rendered (visible or preloaded), so call this after the page has shown up
---(e.g. from `OnPageChanged` or a timer).
---@overload fun(self: View, page1: integer, x1: number, y1: number, page2: integer, x2: number, y2: number): boolean
---@param from PagePosition|number[] `{page=, x=, y=}` or `{page, x, y}`.
---@param to PagePosition|number[]
---@return boolean selected False if the pages aren't rendered or have no text.
function View:select_range(from, to) end

---Selects the text inside a rectangle on one page (from its top-left corner to
---its bottom-right, in reading order). Same requirements as `View:select_range`.
---@param pageno integer
---@param x0 number
---@param y0 number
---@param x1 number
---@param y1 number
---@return boolean selected
function View:select_region(pageno, x0, y0, x1, y1) end

---Scrolls by a number of pixels. Positive values scroll right and down.
---@param dx? integer
---@param dy? integer
function View:scroll(dx, dy) end

---Scrolls to an absolute position in pixels. Omit an axis to leave it unchanged.
---@param x? integer
---@param y? integer
function View:scroll_to(x, y) end

---Current scroll position and its limits, in pixels.
---@return integer x
---@return integer y
---@return integer max_x
---@return integer max_y
function View:scroll_position() end

---Pages currently on screen, in order.
---@return integer[] pages
function View:visible_pages() end

---For a source file and line, jumps to the corresponding position in the PDF.
---The source file must be the absolute path of a file in the TeX project, and the line number must be valid. The PDF must have been compiled with SyncTeX support.
---@param source_file string Absolute path of the source file.
---@param line integer Line number in the source file.
---@param col? integer Column number in the source file (optional).
function View:synctex_forward(source_file, line, col) end

---Size of a page in points (before rotation and zoom). Loads the page if
---needed, so the size is exact.
---@param pageno? integer Page (default: current page).
---@return number? width nil if the page doesn't exist.
---@return number? height
function View:page_size(pageno) end

---@class PageSize
---@field width number
---@field height number
---@field known boolean False if the page was never loaded and this is the document's default size.

---Sizes of a range of pages in points, without loading them. Use
---`View:page_size()` when you need an exact value for one page.
---@param first? integer First page (default: 1).
---@param last? integer Last page (default: last page).
---@return PageSize[] sizes
function View:page_sizes(first, last) end

--- Syncs the given views, so that they scroll and zoom together. The views must be in the current tab; at least two are needed. Replaces an earlier sync of that tab.
---@param ids integer[] List of view IDs to sync.
---@return boolean ok True if the views are synced now.
lektra.view.sync = function(ids) end

--- Stops syncing the views of the current tab.
lektra.view.unsync = function() end

--- Lets the user pick views of the current tab: a number is drawn on each view, the number keys select or deselect it, Enter confirms and Esc cancels. Waits until then.
--- The ids can be given to `lektra.view.sync`.
---@return integer[]? ids The ids of the picked views (in the order they are numbered, empty if none), or nil if cancelled.
lektra.view.pick_views = function() end
