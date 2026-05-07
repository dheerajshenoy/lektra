---@meta
lektra = lektra or {}
lektra.view = {}

---@enum Mode
--- Represents the different interaction modes available in Lektra.
Mode = {
    None = 0,
    VisualLine = 1,
    RegionSelection = 2,
    TextSelection = 3,
    TextHighlight = 4,
    AnnotSelect = 5,
    AnnotRect = 6,
    AnnotPopup = 7,
}

---@enum LayoutMode
--- Represents the different layout modes available in Lektra for displaying documents.
LayoutMode = {
    Single = 0,
    Book = 1,
    Horizontal = 2,
    Vertical = 3,
}


---@class View
--- Represents a View in Lektra.
local View = {}

---@class Location
--- Represents a location in a document.
---@field page integer The page number.
---@field x number The x-coordinate on the page.
---@field y number The y-coordinate on the page.
local Location = {}

---@class OpenFileOptions
--- Options for opening a file in a view.
---@field fit string? The fit mode to use when opening the document (e.g., "width", "height", "window").
---@field zoom number? The zoom level to use when opening the document (e.g., 1.0 for 100%).
local OpenFileOptions = {}

---@class SearchOptions
--- Options for searching in a document.
---@field case_sensitive boolean? Whether the search should be case sensitive (default: false).
---@field whole_word boolean? Whether to match whole words only (default: false).
---@field regex boolean? Whether the query should be treated as a regular expression (default: false
local SearchOptions = {}

-- ##########################################

--- Returns the current page number.
---@return integer pageno
function View:pageno() end

--- Opens a document in the view.
---@overload fun(arg: { file: string, opts: OpenFileOptions }) Opens a document using an argument table with file and opts fields.
---@param file string The path to the document to open.
---@param opts? OpenFileOptions Optional parameters for opening the document (e.g., { fit = "width", zoom = 1.5 }).
function View:open(file, opts) end

--- Close the document.
function View:close() end

--- Goes to the specified page number.
---@param pageno integer
function View:goto_page(pageno) end

--- Returns the total number of pages in the document.
---@return integer pagecount
function View:page_count() end

--- Goto the specified location in the document.
---@param location Location
function View:goto_location(location) end

--- Returns the current location in the document.
---@return Location location
function View:location() end

--- Goes back to the previous location in the document history.
function View:history_back() end

--- Goes forward to the next location in the document history.
function View:history_forward() end

--- Returns the current zoom level of the document.
---@return number zoom
function View:zoom() end

--- Sets the zoom level of the document.
---@param zoom number The desired zoom level (e.g., 1.0 for 100%).
function View:set_zoom(zoom) end

--- Returns the current fit mode of the document.
---@return string mode The current fit mode (e.g., "width", "height", "window").
function View:fit() end

--- Sets the fit mode of the document.
---@param mode string The fit mode (e.g., "width", "height", "window").
function View:set_fit(mode) end

--- Returns the current rotation of the document in degrees.
---@return integer rotation The current rotation (e.g., 0, 90, 180, 270).
function View:rotation() end

--- Sets the rotation of the document.
---@param rotation integer The desired rotation in degrees (e.g., 0, 90, 180, 270).
function View:set_rotation(rotation) end

--- Returns the layout mode of the document.
---@return string layout The current layout mode (e.g., "single", "book", "horizontal", "vertical").
function View:layout() end

--- Sets the layout mode of the document.
---@param layout string The desired layout mode (e.g., "single", "book", "horizontal", "vertical").
function View:set_layout(layout) end

--- Returns true if there is a selection in the document, false otherwise.
---@return boolean has_selection
function View:has_selection() end

--- Returns the text of the current selection in the document.
---@param formatted? boolean Whether to return the selection text with formatting (e.g., newlines, tabs) or as plain text. Default is false (plain text).
---@param page_separator? string An optional string to use as a separator between pages in the selection text when formatted is true. Default is "\n--- Page Break ---\n".
---@return string selection_text
function View:selection_text(formatted, page_separator) end

--- Clears the current selection in the document.
function View:clear_selection() end

--- Searches for the given query in the document and highlights the results.
---@overload fun(arg: { query: string, regex: boolean }) Searches for the query as plain text (default).
---@param query string The search query to find in the document.
---@param regex? boolean Optional parameter indicating whether the query should be treated as a regular expression (default: false).
function View:search(query, regex) end

--- Finds the next occurrence of the last search query and highlights it.
function View:search_hit_next() end

--- Finds the previous occurrence of the last search query and highlights it.
function View:search_hit_previous() end

--- Cancels the current search and clears any search highlights.
function View:search_cancel() end

--- Returns a table of search hits for the last search query. Each hit is represented as a Location object.
---@return Location[] hits
function View:search_hits() end

--- Returns the total number of search hits for the last search query.
---@return integer hit_count
function View:search_hit_count() end

--- Returns the file path of the currently opened document in the view.
---@return string file_path
function View:file_path() end

--- Returns the file type of the currently opened document in the view (e.g., "pdf", "epub").
---@return string file_type
function View:file_type() end

--- Registers a callback function for the specified event on the view.
---@overload fun(event: { name: string, callback: function }) Registers a callback using an event table with name and callback fields.
---@param event string The name of the event to listen for (e.g., "page_changed", "selection_changed").
---@param callback function The callback function to be called when the event occurs. The callback function will receive the view instance and any relevant event data as arguments.
---@return integer handle A unique handle that can be used to unregister the callback later.
function View:register(event, callback) end

--- Unregisters a callback function for the specified event on the view.
---@overload fun(event: { name: string, callback: function, handle: integer}) Unregisters a callback using an event table with name and callback fields.
---@param event string The name of the event to stop listening for (e.g., "page_changed", "selection_changed").
---@param handle integer The unique handle returned by the `on` method when the callback was registered.
function View:unregister(event, handle) end

--- Registers a one-time callback function for the specified event on the view. The callback will be automatically unregistered after it is called once.
---@overload fun(event: { name: string, callback: function }) Registers a one-time callback using an event table with name and callback fields.
---@param event string The name of the event to listen for (e.g., "page_changed", "selection_changed").
---@param callback function The callback function to be called when the event occurs. The callback function will receive the view instance and any relevant event data as arguments.
function View:once(event, callback) end

--- Registers a callback to customize the context menu for selections.
---@param menu_type string "TextSelection" or "RegionSelection"
---@param callback function Callback invoked with (view, menu)
---@return integer handle
function View:register_context_menu(menu_type, callback) end

--- Unregisters a context menu callback.
---@param menu_type string "TextSelection" or "RegionSelection"
---@param handle integer
function View:unregister_context_menu(menu_type, handle) end

--- Returns true if the document in the view has unsaved changes, false otherwise.
function View:is_modified() end

--- Saves the current document. If the document has unsaved changes, it will be saved to its current file path.
function View:save() end

--- Saves the current document to a new file path. If the document has unsaved changes, it will be saved to the specified file path.
---@param file_path string The file path to save the document to.
function View:save_as(file_path) end

--- Returns the text content of the document in the view. This may return an empty string for certain file types (e.g., images) or if the document does not contain extractable text.
---@param formatted boolean Whether to return the text content with formatting (e.g., newlines, tabs) or as plain text. Default is false (plain text).
---@return string text_content
function View:extract_text(formatted) end

--- Returns the unique identifier of the view.
---@return integer id
function View:id() end

--- Returns the spacing between pages in the document view, in pixels.
---@return integer spacing
function View:spacing() end

--- Gets the current mode of the view
---@return Mode mode The current interaction mode (e.g., "select", "pan", "zoom").
function View:mode() end

--- Sets the interaction mode of the view
---@param mode Mode The desired interaction mode
function View:set_mode(mode) end

--- Sets the device pixel ratio (DPR) for the view
---@param dpr number The desired device pixel ratio (e.g., 1.0 for standard displays, 2.0 for high-DPI displays). Setting the DPR can improve rendering quality on high-DPI screens.
function View:set_dpr(dpr) end

--- Gets the current device pixel ratio (DPR) for the view
---@return number dpr The current device pixel ratio (e.g., 1.0 for standard displays, 2.0 for high-DPI displays).
function View:dpr() end

--- Sets whether to invert the colors of the document in the view. Inverting colors can be useful for reducing eye strain or improving readability in low-light conditions.
---@param invert boolean True to enable color inversion, false to disable it.
function View:set_invert(invert) end

--- Gets whether color inversion is currently enabled for the document in the view.
---@return boolean invert True if color inversion is enabled, false otherwise.
function View:is_invert() end

--- Returns if the document has unsaved changes.
function View:is_modified() end

--- Sets the layout mode of the document in the view. The layout mode determines how pages are arranged in the view (e.g., single page, book view, horizontal scrolling, vertical scrolling).
---@param layout LayoutMode The desired layout mode
function View:set_layout(layout) end

--- Gets the current layout mode of the document in the view. The layout mode determines how pages are arranged in the view (e.g., single page, book view, horizontal scrolling, vertical scrolling).
---@return LayoutMode layout The current layout mode
function View:layout() end

--- Returns the portal associated with the view, if any.
---@return View portal The portal view associated with the current view, or nil if there is no portal.
function View:portal() end

--- Returns true if the view is a portal, false otherwise.
---@return boolean is_portal True if the view is a portal, false otherwise.
function View:is_portal() end

--- Sets the portal for the view. A portal is a secondary view that can be used to display a different document or a different part of the same document. When a portal is set for a view, the portal's content will be displayed alongside the main view, allowing for side-by-side comparison or reference.
---@param portal_view View The view to set as the portal for the current view. Setting a
function View:set_portal(portal_view) end

--- Sets the active state of the view. An active view is the one that currently has focus and receives user input. Setting a view as active will bring it to the foreground and allow the user to interact with it.
---@param active boolean True to set the view as active, false to deactivate it.
function View:set_active(active) end

--- Returns true if the view is currently active, false otherwise. An active view is the one that currently has focus and receives user input.
---@return boolean is_active True if the view is active, false otherwise.
function View:is_active() end

--- Enables or disables visual line mode for the view. Visual line mode is a display mode that wraps lines of text at the edge of the view, allowing for easier reading of long lines without horizontal scrolling. When visual line mode is enabled, lines will be wrapped based on the width of the view, and the user can navigate through the wrapped lines as if they were separate lines.
---@param enabled boolean True to enable visual line mode, false to disable it.
function View:set_visual_line_mode(enabled) end

--- Returns true if visual line mode is currently enabled for the view, false otherwise. Visual line mode is a display mode that wraps lines of text at the edge of the view, allowing for easier reading of long lines without horizontal scrolling.
---@return boolean enabled True if visual line mode is enabled, false otherwise.
function View:is_visual_line_mode() end

--- Returns true if the view is currently in thumbnail view mode, false otherwise. Thumbnail view mode is a display mode that shows small thumbnail images of each page in the document, allowing for quick navigation and overview of the document's structure.
---@return boolean is_thumbnail True if the view is in thumbnail view mode, false otherwise.
function View:is_thumbnail_view() end


-- ##########################################


--- Gets the view with the given id.
--- @param id? integer
--- @return View view
lektra.view.get = function(id) end

--- Returns the current View object for the active document in the current tab.
--- `Note` If there are multiple documents open in the current tab, it will return the active document's view. If no documents are open, it will return nil.
--- @return View view
lektra.view.current = function() end

--- Returns a table of View objects for the current tab.
--- @param tabindex? integer The tab index to get the document for.
---
---`Note` If not provided, it will return all documents in the current tab.
--- @return View[] documents
lektra.view.list = function(tabindex) end
