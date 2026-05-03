---@meta
lektra = lektra or {}
lektra.view = {}

---@class View
--- Represents a View in Lektra.
---@field id integer The unique identifier of the document.
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
---@param formatted boolean Whether to return the selection text with formatting (e.g., newlines, tabs) or as plain text. Default is false (plain text).
---@return string selection_text
function View:selection_text(formatted) end

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

-- ##########################################

--- Gets the view with the given id.
--- @param id? integer
--- @return View view
lektra.view.get = function(id) end

--- Returns a table of View objects for the current tab.
--- @param tabindex? integer The tab index to get the document for.
---
---`Note` If not provided, it will return all documents in the current tab.
--- @return View[] documents
lektra.view.list = function(tabindex) end
