---@meta

lektra = lektra or {}
lektra.ui = {}

---@class MenuItem
---@field label string Label to show in the menu for this item
---@field callback function Function to call when this menu item is selected
---@field submenu? MenuItem[] Optional list of submenu items to show when this menu item is hovered over, if this field is present the "callback" field will be ignored
---@field icon? string Optional path to an icon file to show next to the menu item

---@class Menu
---@field show fun(): nil Show the menu at the current cursor position
---@field add_item fun(label: string, callback?: function): nil Add an item to the menu

--- Options for the picker dialog
---@class FileDialogOptions
---@field title string Title of the file dialog
---@field directory string Initial directory to open in the file dialog, default is the current working directory
---@field filter string File filter to apply in the file dialog, default is "*.*"

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

--- Shows a file dialog to the user and returns the selected file path as a string
---@overload fun(arg: {title: string, directory?: string, filter?: string}): string[]
---@param mode string "open" or "save"
---@param options FileDialogOptions Options for the file dialog
---@return string[] FilePaths List of selected file paths
lektra.ui.file_dialog = function(mode, options) end

--- Shows a color picker dialog to the user and returns the selected color as a string in hex format (e.g., "#RRGGBB")
---@overload fun(arg: {colors: string[], default_color?: string}): string
---@param colors string[] List of color options to show in the color picker, specified as hex color strings (e.g., "#FF0000" for red)
---@param default_color string Default color to be selected when the color picker is shown, specified as a hex color string (e.g., "#00FF00" for green)
---@return string Selected color in hex format (e.g., "#RRGGBBAA")
lektra.ui.color_dialog = function(colors, default_color) end

--- Shows a context menu to the user with the given items and options
---@param menu_items MenuItem[] List of menu items to show in the context menu, each item is a table with a "label" field (string), "callback" field (function) and an optional "icon" field (string, path to an icon file)
---@return Menu
lektra.ui.menu = function(menu_items) end
