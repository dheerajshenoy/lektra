---@meta
lektra = lektra or {}
lektra.bookmarks = {}

---@class Bookmark
---@field id string Bookmark uuid.
---@field file_path string File the bookmark is in.
---@field pageno integer Page number of the bookmark (1-based).
---@field x number Horizontal position on the page.
---@field y number Vertical position on the page.
---@field created string When the bookmark was created.

--- Adds a bookmark. Without an argument, bookmarks the current location of the current view.
---@param bookmark? {file_path?: string, pageno?: integer, x?: number, y?: number} Where to put it (pageno is 1-based); anything left out comes from the current view.
---@return string id The new bookmark's id.
lektra.bookmarks.add = function (bookmark) end

--- Lists all the bookmarks
---@return Bookmark[] bookmarks list of bookmarks
lektra.bookmarks.list = function() end
