---@meta
lektra = lektra or {}
lektra.clipboard = {}

--- The text on the clipboard ("" if there is none).
---@return string text
lektra.clipboard.get = function() end

--- Puts text on the clipboard.
---@param text string
lektra.clipboard.set = function(text) end
