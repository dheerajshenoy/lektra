---@meta

lektra = lektra or {}
lektra.tabs = {}


--- Closes the tab at the specified index.
---@param index integer The index of the tab to close.
lektra.tabs.close = function(index) end

--- Switches to the tab at the specified index.
---@param index integer The index of the tab to close.
lektra.tabs.goto = function(index) end

--- Switches to the last active tab.
lektra.tabs.last = function() end

--- Switches to the first tab.
lektra.tabs.first = function() end

--- Switches to the next tab.
lektra.tabs.next = function() end

--- Switches to the previous tab.
lektra.tabs.prev = function() end

--- Moves the current tab to the right.
lektra.tabs.move_right = function() end

--- Moves the current tab to the left.
lektra.tabs.move_left = function() end

--- Returns the number of open tabs.
--- @return integer
lektra.tabs.count = function() end

--- Returns the index of the current tab.
--- @return integer
lektra.tabs.current = function() end

--- Returns a table of {index, title} for each tab
--- @return {index: integer, title: string}[]
lektra.tabs.list = function() end
