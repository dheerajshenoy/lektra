---@meta

lektra = lektra or {}
lektra.cmd = {}

--- Registers a command with the given name and callback function.
---@overload fun(arg: {name: string, callback: function, desc?: string})
---@param name string The name of the command to register.
---@param callback function The function to call when the command is executed. It should accept a single argument, which is a table of the command's arguments.
---@param desc? string A description of the command, which will be shown in the help menu.
lektra.cmd.register = function(name, callback, desc) end

--- Unregisters a command with the given name.
---@overload fun(arg: {name: string})
--- @param name string The name of the command to unregister.
lektra.cmd.unregister = function(name) end

--- Executes a command with the given name and arguments.
---@overload fun(arg: {name: string, args?: table})
---@param name string The name of the command to execute.
---@param args? table A table of arguments to pass to the command's callback function.
lektra.cmd.execute = function(name, args) end

