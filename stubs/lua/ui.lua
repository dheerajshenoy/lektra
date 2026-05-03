---@meta

lektra = lektra or {}
lektra.ui = {}

---@class PickerOptions
---@field flat boolean Whether to show items in a flat list or a tree view, default is false (tree view)
---@field columns table List of column names to display in the picker, default is {"Value"}
---@field on_accept function Callback function that is called when the user accepts the picker dialog, receives the selected item as an argument
---@field on_cancel function Callback function that is called when the user cancels the picker dialog

--- Shows a message to the user with `QMessageBox`
---@overload fun(arg: {title: string, message: string, type?: string})
---@param title string
---@param message string
---@param type? string "info", "warning", "error", default is "info"
lektra.ui.messagebox = function(title , message, type) end

--- Shows a message to the user in the status bar
---@overload fun (arg: {message: string, duration?: number})
---@param message string Message to show in the status bar
---@param duration? number Duration in seconds to show the message, default is 5 seconds
lektra.ui.message = function(message, duration) end

--- Shows an input dialog to the user with `QInputDialog` and returns the input as a string
---@overload fun(arg: {title: string, prompt: string})
---@return string
---@param title string
---@param prompt string
lektra.ui.input = function(title, prompt) end

--- Shows a picker dialog to the user and returns the selected item as a string.
--- Items can be heirarchical specified as a table of tables, can be strings or numbers,
--- but will be converted to strings for display
---@overload fun(arg: {prompt: string, items: table, options?: PickerOptions})
---@return string
---@param prompt string
---@param items table
---@param options? PickerOptions
lektra.ui.picker = function (prompt, items, options) end

