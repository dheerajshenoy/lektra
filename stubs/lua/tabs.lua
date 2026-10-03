---@meta

lektra = lektra or {}
lektra.tabs = {}

---@class Tab
local Tab = {}

--- Returns the unique identifier for the tab.
---@return integer|nil ID The ID of the tab.
function Tab:id() end

--- Close the tab
function Tab:close() end

--- Returns the title of the tab.
---@return string title The title of the tab.
function Tab:title() end

--- Gives the tab a title of its own, kept until it is reset.
---@param title? string New title; nil or an empty string restores the default (the file name).
function Tab:rename(title) end

--- Returns the View object associated with the tab.
---@return View view The View object for the tab.
function Tab:view() end

--- Returns the index of the tab (0-based).
---@return integer index The index of the tab.
function Tab:index() end

-- ###########################################################

--- Closes the tab with the specified unique identifier.
---@param id integer The index of the tab to close.
lektra.tabs.close = function(id) end

--- Switches to the tab with the specified unique identifier.
---@param id integer The index of the tab to switch to.
lektra.tabs.goto = function(id) end

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

--- Returns the Tab object for the current tab.
---@return Tab
lektra.tabs.current = function() end

--- Returns a table of tab objects
--- @return Tab[] tabs List of Tab objects
lektra.tabs.list = function() end

--- Gives a tab a title of its own, kept until it is reset.
---@param index integer Tab index (0-based).
---@param title? string New title; nil or an empty string restores the default (the file name).
lektra.tabs.rename = function(index, title) end

--- Returns the stable unique identifier for a tab index (0-based).
---@param index integer
---@return string id
lektra.tabs.get_id = function(index) end

-- ###########################################################
-- Multi-tab selection and operations.
--
-- Tabs can be selected with Ctrl/Shift+click or from Lua. The operations below
-- take an optional list of tab indices (0-based); without one they act on the
-- selected tabs, or on the current tab when nothing is selected.

--- Returns the indices of the selected tabs (0-based).
---@return integer[] indices
lektra.tabs.selected = function() end

--- Selects or deselects a tab.
---@param index integer Tab index (0-based).
---@param selected? boolean Default: true.
lektra.tabs.select = function(index, selected) end

--- Selects every tab.
lektra.tabs.select_all = function() end

--- Clears the tab selection.
lektra.tabs.clear_selection = function() end

--- Closes tabs the same way as clicking their close button.
---@param indices? integer[] Tabs to close (default: the selected tabs, else the current tab).
lektra.tabs.close_selected = function(indices) end

--- Merges tabs into one tab, with each document in its own split.
---@param mode "vertical"|"horizontal" Direction the splits are laid out in.
---@param indices? integer[] Tabs to merge (default: the selected tabs, else the current tab).
lektra.tabs.merge = function(mode, indices) end

--- Moves the splits of tabs into separate tabs, the opposite of `merge`.
---@param indices? integer[] Tabs to split up (default: the selected tabs, else the current tab).
lektra.tabs.split_out = function(indices) end

--- Moves tabs into a new window.
---@param indices? integer[] Tabs to move (default: the selected tabs, else the current tab).
lektra.tabs.move_to_window = function(indices) end

--- Saves tabs as a session.
---@param name? string Session name; omit to be asked for one.
---@param indices? integer[] Tabs to save (default: the selected tabs, else the current tab).
lektra.tabs.save_session = function(name, indices) end
