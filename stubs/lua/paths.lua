---@meta
lektra = lektra or {}
lektra.paths = {}

--- The folder of the configuration files (`init.lua`, `config.toml`). Created if it does not exist.
---@return string path Absolute path, without a trailing slash.
lektra.paths.config = function() end

--- The folder Lektra keeps its data in (history, bookmarks, sessions...), a good place for files a script wants to keep. Created if it does not exist.
---@return string path Absolute path, without a trailing slash.
lektra.paths.data = function() end

--- The folder for files that can be thrown away and made again. Created if it does not exist.
---@return string path Absolute path, without a trailing slash.
lektra.paths.cache = function() end
