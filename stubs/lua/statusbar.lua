---@meta
lektra = lektra or {}
lektra.statusbar = {}

---@class StatusbarSegment
---@field text? string The text to show; empty or nil hides the segment.
---@field tooltip? string Shown when the mouse rests on it.

---@class StatusbarSegmentOptions
---@field interval? number Ask the provider again every this many seconds (at least 0.1), besides when the statusbar changes.
---@field tooltip? string Shown when the mouse rests on the segment.
---@field on_click? fun() Called when the segment is clicked.

--- Adds a segment to the statusbar. It is a module like "page" or "zoom": it goes where `statusbar.layout` names it, or to the right end if the layout does not.
--- The provider is called (with the current view, or nil) when the statusbar changes (page, zoom, file, mode...) and every `interval` seconds if given; it returns the text, a table `{ text = , tooltip = }`, or nil to hide the segment. A string instead of a function is a fixed text.
--- Registering a name again replaces the segment. The names of the built-in modules (session, filename, page, zoom, progress, mode, portal, narrow) cannot be used.
---@param name string
---@param provider fun(view: View|nil): string|StatusbarSegment|nil|false
---@param opts? StatusbarSegmentOptions
lektra.statusbar.register = function(name, provider, opts) end

--- Shows `text` in a segment (creating it), without a provider. Use it from callbacks, for example to show the progress of a job. `nil` or an empty text hides it.
---@param name string
---@param text string|nil
---@param opts? { tooltip?: string, on_click?: fun() }
lektra.statusbar.set = function(name, text, opts) end

--- Asks the provider of a segment (or of all segments) again now.
---@param name? string
lektra.statusbar.update = function(name) end

--- Removes a segment.
---@param name string
---@return boolean removed False if there was no such segment.
lektra.statusbar.unregister = function(name) end
