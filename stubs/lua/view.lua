---@meta

lektra = lektra or {}
lektra.view = {}

---@class View
--- DocumentView*

--- Returns the current view id.
--- @return integer id
lektra.view.current = function() end

--- Gets the view with the given id.
--- @param id integer
--- @return View view
lektra.view.get = function(id) end

--- Returns a table of View
--- @param tabindex? integer The tab index to get the views for.
---
---`Note` If not provided, it will return all views in the current tab.
--- @return View[] views
lektra.view.list = function(tabindex) end

