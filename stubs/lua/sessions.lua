---@meta

lektra = lektra or {}

-- ###########################################################
-- Sessions

lektra.sessions = {}

--- Names of the saved sessions.
---@return string[] names
lektra.sessions.list = function() end

--- Whether a session with this name exists.
---@param name string
---@return boolean exists
lektra.sessions.exists = function(name) end

--- Loads a session. It opens in a new window if this window already has
--- documents open, like the `session_load` command.
---@param name string
---@return boolean ok
---@return string? error "no such session"
lektra.sessions.load = function(name) end

--- Deletes a saved session.
---@param name string
---@return boolean deleted False if there is no such session.
lektra.sessions.delete = function(name) end

-- To save a session use `lektra.tabs.save_session()`.

-- ###########################################################
-- Recent files

---@class RecentFile
---@field file_path string
---@field page integer Page the file was last left on.
---@field last_accessed integer When it was last opened, in seconds since the Unix epoch.

lektra.recent_files = {}

--- Recently opened files, most recent first.
---@return RecentFile[] files
lektra.recent_files.list = function() end

-- ###########################################################
-- Windows

lektra.window = {}

--- Starts a new window.
---@param file? string Document to open in it; without one the window starts empty.
---@return boolean started
lektra.window.open = function(file) end
