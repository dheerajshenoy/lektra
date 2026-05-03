# lektra Lua API

This document mirrors the Lua stub definitions under `stubs/lua/` and gives
short usage examples for scripting Lektra.

## Modules

All Lua APIs live under the global `lektra` table.

### lektra.view

Document view helpers and per-document actions.

#### Types

- `View`
  - `id: integer` Unique identifier of the document.
- `Location`
  - `page: integer` Page number.
  - `x: number` X coordinate on the page.
  - `y: number` Y coordinate on the page.
- `OpenFileOptions`
  - `fit: string?` Fit mode (`"width"`, `"height"`, `"window"`).
  - `zoom: number?` Zoom level (e.g. `1.0` for 100%).
- `SearchOptions`
  - `case_sensitive: boolean?` Case sensitive search.
  - `whole_word: boolean?` Whole word only.
  - `regex: boolean?` Treat query as regex.

#### View methods

- `view:pageno() -> integer`
  - Return current page number.
- `view:open(file: string, opts?: OpenFileOptions)`
  - Open a document in the current view.
  - Overload: `view:open({ file = "path", opts = { ... } })`.
- `view:close()`
  - Close the document in the current view.
- `view:goto_page(pageno: integer)`
  - Go to a page number.
- `view:page_count() -> integer`
  - Total number of pages.
- `view:goto_location(location: Location)`
  - Jump to a location object.
- `view:location() -> Location`
  - Current location.
- `view:history_back()`
  - Go back in location history.
- `view:history_forward()`
  - Go forward in location history.
- `view:zoom() -> number`
  - Current zoom level.
- `view:set_zoom(zoom: number)`
  - Set zoom level.
- `view:fit() -> string`
  - Current fit mode.
- `view:set_fit(mode: string)`
  - Set fit mode.
- `view:rotation() -> integer`
  - Rotation in degrees.
- `view:set_rotation(rotation: integer)`
  - Set rotation in degrees.
- `view:layout() -> string`
  - Layout mode (`"single"`, `"book"`, `"horizontal"`, `"vertical"`).
- `view:set_layout(layout: string)`
  - Set layout mode.
- `view:has_selection() -> boolean`
  - Whether there is a selection.
- `view:selection_text(formatted: boolean) -> string`
  - Return current selection text.
- `view:clear_selection()`
  - Clear selection.
- `view:search(query: string, regex?: boolean)`
  - Search for query, optionally as regex.
  - Overload: `view:search({ query = "...", regex = true })`.
- `view:search_hit_next()`
  - Jump to next hit.
- `view:search_hit_previous()`
  - Jump to previous hit.
- `view:search_cancel()`
  - Cancel search and clear highlights.
- `view:search_hits() -> Location[]`
  - List of hit locations.
- `view:search_hit_count() -> integer`
  - Number of hits.
- `view:file_path() -> string`
  - Current file path.
- `view:file_type() -> string`
  - File type (`"pdf"`, `"epub"`, ...).
- `view:register(event: string, callback: function) -> integer`
  - Register a view event callback.
  - Overload: `view:register({ name = "event", callback = fn })`.
- `view:unregister(event: string, handle: integer)`
  - Unregister a previously registered callback.
  - Overload: `view:unregister({ name = "event", handle = id })`.
- `view:once(event: string, callback: function)`
  - Register a one-shot callback.
- `view:is_modified() -> boolean`
  - Whether the document has unsaved changes.
- `view:save()`
  - Save document.
- `view:save_as(file_path: string)`
  - Save document to a new path.
- `view:extract_text(formatted: boolean) -> string`
  - Extract full document text for the current page.

#### Module functions

- `lektra.view.get(id?: integer) -> View`
  - Get view by id.
- `lektra.view.current() -> View`
  - Get current active view, or `nil` if none.
- `lektra.view.list(tabindex?: integer) -> View[]`
  - List views in a tab or in current tab.

#### Example

```lua
local view = lektra.view.current()
if view then
  print(view:page_count())
  view:goto_page(1)
end
```

### lektra.ui

UI helpers.

#### Types

- `PickerOptions`
  - `flat: boolean` Flat list vs tree view.
  - `columns: table` Column names (default `{ "Value" }`).
  - `on_accept: function` Called when item accepted.
  - `on_cancel: function` Called on cancel.

#### Functions

- `lektra.ui.messagebox(title: string, message: string, type?: string)`
  - `type` is `"info"`, `"warning"`, or `"error"`.
  - Overload: `lektra.ui.messagebox({ title = "...", message = "...", type = "info" })`.
- `lektra.ui.message(message: string, duration?: number)`
  - Show status message in seconds.
  - Overload: `lektra.ui.message({ message = "...", duration = 5 })`.
- `lektra.ui.input(title: string, prompt: string) -> string`
  - Overload: `lektra.ui.input({ title = "...", prompt = "..." })`.
- `lektra.ui.picker(prompt: string, items: table, options?: PickerOptions) -> string`
  - Overload: `lektra.ui.picker({ prompt = "...", items = { ... }, options = { ... } })`.

#### Example

```lua
lektra.ui.message("Hello", 2)
```

### lektra.cmd

Register and execute custom commands.

#### Functions

- `lektra.cmd.register(name: string, callback: function, desc?: string)`
  - Overload: `lektra.cmd.register({ name = "...", callback = fn, desc = "..." })`.
- `lektra.cmd.unregister(name: string)`
  - Overload: `lektra.cmd.unregister({ name = "..." })`.
- `lektra.cmd.execute(name: string, args?: table)`
  - Overload: `lektra.cmd.execute({ name = "...", args = { ... } })`.

#### Example

```lua
lektra.cmd.register("hello", function()
  lektra.ui.message("Hi from Lua", 2)
end, "Say hello")
```

### lektra.event

Global event callbacks.

#### Functions

- `lektra.event.register(event: string, callback: function) -> integer`
  - Overload: `lektra.event.register({ event = "...", callback = fn })`.
- `lektra.event.unregister(event: string, handle: integer)`
  - Overload: `lektra.event.unregister({ event = "...", handle = id })`.
- `lektra.event.once(event: string, callback: function)`
  - Overload: `lektra.event.once({ event = "...", callback = fn })`.
- `lektra.event.clear(event: string)`
  - Clear all callbacks for an event.

#### Example

```lua
lektra.event.once("app_ready", function()
  lektra.ui.message("Ready", 2)
end)
```

### lektra.keymap

Keybinding helpers.

#### Functions

- `lektra.keymap.set(name: string, value: string)`
  - Set keybinding for an action.
- `lektra.keymap.unset(name: string)`
  - Remove keybinding for an action.

#### Example

```lua
lektra.keymap.set("zoom_in", "Ctrl++")
```

### lektra.mousemap

Mouse binding helpers.

#### Functions

- `lektra.mousemap.set(name: string, value: string)`
  - Set mouse binding (e.g. `"Ctrl+LeftButton"`).
- `lektra.mousemap.unset(name: string)`
  - Remove mouse binding.

#### Example

```lua
lektra.mousemap.set("pan", "Alt+LeftButton")
```

### lektra.tabs

Tab management.

#### Functions

- `lektra.tabs.close(index: integer)`
  - Close a tab.
- `lektra.tabs.goto(index: integer)`
  - Switch to a tab.
- `lektra.tabs.last()`
  - Switch to last active tab.
- `lektra.tabs.first()`
  - Switch to first tab.
- `lektra.tabs.next()`
  - Switch to next tab.
- `lektra.tabs.prev()`
  - Switch to previous tab.
- `lektra.tabs.move_right()`
  - Move current tab right.
- `lektra.tabs.move_left()`
  - Move current tab left.
- `lektra.tabs.count() -> integer`
  - Number of open tabs.
- `lektra.tabs.current() -> integer`
  - Current tab index.
- `lektra.tabs.list() -> { index: integer, title: string }[]`
  - List tab index and title.

#### Example

```lua
for _, tab in ipairs(lektra.tabs.list()) do
  print(tab.index, tab.title)
end
```

## Event Names

These names are case-sensitive and must match exactly.

- `OnAppReady`
- `OnReady`
- `OnFileOpen`
- `OnFileClose`
- `OnPageChanged`
- `OnZoomChanged`
- `OnLinkClicked`
- `OnTabChanged`
- `OnTextSelected`

## Predefined Strings

### Fit modes

- `width`
- `height`
- `window`

### Layout modes

- `single`
- `book`
- `horizontal`
- `vertical`

### Message box types

- `info`
- `warning`
- `error`

### Default mousemap action names

- `pan`
- `preview`
- `portal`
- `synctex_jump`
