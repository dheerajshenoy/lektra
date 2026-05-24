# lektra Lua API

All Lua APIs live under the global `lektra` table.

---

## lektra.view

Document view helpers and per-document actions.

### Types

**`View`** — opaque userdata returned by view functions.

**`Location`** — three separate return values, not a table:
- `pageno: integer` — 1-based page number.
- `x: number` — X coordinate on the page.
- `y: number` — Y coordinate on the page.

**`OutlineEntry`** — one node in the document outline tree:
- `title: string` — Entry text.
- `pageno: integer | nil` — 1-based destination page; `nil` for external or destination-less links.
- `x: number` — X jump coordinate on the destination page.
- `y: number` — Y jump coordinate on the destination page.
- `children: OutlineEntry[]` — Child entries (empty table for leaf nodes).

### View methods

| Method | Returns | Description |
|---|---|---|
| `view:pageno()` | `integer` | Current page number (1-based). |
| `view:page_count()` | `integer` | Total pages. |
| `view:goto_page(n)` | — | Go to page `n` (1-based). |
| `view:goto_location(n, x, y)` | — | Jump to page `n` at coordinates `x`, `y`. |
| `view:location()` | `pageno, x, y` | Current location (three return values). |
| `view:history_back()` | — | Go back in navigation history. |
| `view:history_forward()` | — | Go forward in navigation history. |
| `view:zoom()` | `number` | Current zoom level (1.0 = 100%). |
| `view:set_zoom(z)` | — | Set zoom level. |
| `view:fit()` | `integer` | Current fit mode (see `lektra.opt.FitMode`). |
| `view:set_fit(mode)` | — | Set fit mode integer. |
| `view:rotation()` | `number` | Rotation in degrees. |
| `view:set_rotation(r)` | — | Set rotation in degrees. |
| `view:rotate_clock()` | — | Rotate the page 90° clockwise. |
| `view:rotate_anticlock()` | — | Rotate the page 90° counter-clockwise. |
| `view:flip_horizontal()` | — | Toggle horizontal flip. |
| `view:flip_vertical()` | — | Toggle vertical flip. |
| `view:layout()` | `integer` | Current layout mode (see `lektra.opt.LayoutMode`). |
| `view:set_layout(mode)` | — | Set layout mode integer. |
| `view:dpr()` | `number` | Device pixel ratio. |
| `view:set_dpr(r)` | — | Set device pixel ratio. |
| `view:spacing()` | `number` | Page spacing in pixels. |
| `view:is_invert()` | `boolean` | Whether colour inversion is active. |
| `view:set_invert(b)` | — | Enable or disable colour inversion. |
| `view:is_modified()` | `boolean` | Whether the document has unsaved changes. |
| `view:is_active()` | `boolean` | Whether this view is the focused split. |
| `view:set_active(b)` | — | Set focus state. |
| `view:is_image()` | `boolean` | Whether the open file is an image. |
| `view:is_portal()` | `boolean` | Whether this view is a portal clone. |
| `view:is_visual_line_mode()` | `boolean` | Whether visual-line mode is on. |
| `view:set_visual_line_mode(b)` | — | Enable or disable visual-line mode. |
| `view:is_thumbnail_view()` | `boolean` | Whether this is a thumbnail panel view. |
| `view:id()` | `integer` | Stable unique ID for this view. |
| `view:file_path()` | `string` | Path of the open document. |
| `view:file_type()` | `string` | File type string (`"pdf"`, `"epub"`, …). |
| `view:open(path)` | — | Open a file in this view. |
| `view:close()` | — | Close the document in this view. |
| `view:reload()` | — | Reload the file from disk. |
| `view:save()` | — | Save the document. |
| `view:save_as()` | — | *(Not yet implemented — raises an error.)* |
| `view:undo()` | — | Undo the last action. |
| `view:redo()` | — | Redo the last undone action. |
| `view:extract_text(formatted)` | `string` | Extract text from the current page. |
| `view:has_selection()` | `boolean` | Whether text is selected. |
| `view:selection_text(formatted)` | `string` | Selected text. |
| `view:clear_selection()` | — | Clear the current selection. |
| `view:search(query, regex?)` | — | Search the document. |
| `view:search_hit_next()` | — | Jump to next search hit. |
| `view:search_hit_prev()` | — | Jump to previous search hit. |
| `view:search_cancel()` | — | Cancel search and clear highlights. |
| `view:search_hit_count()` | `integer` | Number of current search hits. |
| `view:mode()` | `integer` | Current selection/interaction mode. |
| `view:set_mode()` | — | *(Not yet implemented — raises an error.)* |
| `view:outline()` | `OutlineEntry[]` | Document outline (table of contents) as a tree. Returns an empty table if the document has no outline. |

#### Per-view event listeners

- `view:register(event: string, callback: function) -> integer`
  Register a callback for a view-level event. Returns a handle for later removal.
  Per-view events use **string names** (unlike `lektra.event` which uses integer constants).

- `view:unregister(event: string, handle: integer)`
  Remove a previously registered callback by handle.

- `view:register_once(event: string, callback: function)`
  Register a one-shot callback that fires once then removes itself.

- `view:clear_listeners(event: string)`
  Remove all callbacks for a view-level event.

#### Context menu listeners

- `view:register_context_menu(type: string, callback: function) -> integer`
  `type` is `"TextSelection"` or `"RegionSelection"`.
  Callback receives `(view, menu)`. `menu` supports `menu:add_item(label, fn)`.

- `view:unregister_context_menu(type: string, handle: integer)`
  Remove a context menu callback by handle.

### Module functions

- `lektra.view.current() -> View | nil`
  Active view, or `nil` if no document is open.

- `lektra.view.get(id: integer) -> View | nil`
  Look up a view by its stable ID.

- `lektra.view.list(tab_index: integer) -> View[]`
  All views in a given tab (by 0-based tab index).

### Example

```lua
local v = lektra.view.current()
if v then
    print(v:file_path(), v:pageno(), v:page_count())
    v:goto_page(1)
    v:set_zoom(1.5)
end
```

---

## lektra.ui

UI helpers.

### Functions

- `lektra.ui.messagebox(title, message, type?)`
  Blocking dialog. `type` is `"info"`, `"warning"`, or `"error"` (default `"info"`).

- `lektra.ui.message(message, duration?)`
  Status-bar toast. `duration` is seconds (default 3).

- `lektra.ui.input(title, prompt) -> string | nil`
  Text input dialog. Returns `nil` if cancelled.

- `lektra.ui.file_dialog(mode?, options?) -> string | nil`
  File picker. `mode` is `"open"` or `"save"` (default `"open"`).
  `options` table: `default_path`, `filters` (Qt filter string e.g. `"PDF (*.pdf);;All (*.*)"`).

- `lektra.ui.color_dialog(colors: string[]) -> string | nil`
  Colour picker seeded with a list of colour strings.
  Returns selected colour as `#AARRGGBB`, or `nil` if cancelled.

- `lektra.ui.picker(prompt, items, options?) -> string | nil`
  General-purpose picker widget.
  `options` table: `flat` (boolean), `columns` (string[]), `on_accept` (fn), `on_cancel` (fn).

- `lektra.ui.menu(items) -> Menu`
  Create a popup menu. Each item: `{ label, callback, submenu?, icon? }`.
  `menu:show()` — display the menu at the cursor.
  `menu:add_item(label, callback)` — add an item dynamically.

### Example

```lua
local file = lektra.ui.file_dialog("open", { filters = "PDF (*.pdf);;All (*.*)" })
if file then
    lektra.ui.message("Opened: " .. file, 3)
end
```

---

## lektra.cmd

Register and execute commands. Registered commands appear in the command palette.

### Functions

- `lektra.cmd.register(name, callback, desc?)`
  Positional form. `desc` is shown in the command palette.

- `lektra.cmd.register({ name=, callback=, desc= })`
  Table form.

- `lektra.cmd.unregister(name)`
  Remove a previously registered command. Frees the Lua function reference.

- `lektra.cmd.execute(name, args?) -> boolean`
  Run a command by name. `args` is a string array. Returns `true` on success.

- `lektra.cmd.list() -> { name: string, desc: string }[]`
  All registered commands.

- `lektra.cmd.alias(alias, target)`
  Create an alias that calls an existing command.

### Example

```lua
lektra.cmd.register("word_count", function(args)
    local v = lektra.view.current()
    if v then
        local text = v:extract_text(false)
        local words = select(2, text:gsub("%S+", ""))
        lektra.ui.message("Words: " .. words, 3)
    end
end, "Count words on current page")
```

---

## lektra.event

Global (app-level) event subscriptions. Uses integer `EventType` constants from
`lektra.event.EventType`.

### EventType constants

```lua
lektra.event.EventType.OnAppReady
lektra.event.EventType.OnReady
lektra.event.EventType.OnFileOpen
lektra.event.EventType.OnFileClose
lektra.event.EventType.OnPageChanged
lektra.event.EventType.OnZoomChanged
lektra.event.EventType.OnLinkClicked
lektra.event.EventType.OnTextSelected
lektra.event.EventType.OnTabChanged
lektra.event.EventType.OnSearchStarted
lektra.event.EventType.OnSearchFinished
lektra.event.EventType.OnSearchCancelled
lektra.event.EventType.OnAnnotationAdded
lektra.event.EventType.OnAnnotationRemoved
lektra.event.EventType.OnRegionSelectionContextMenuRequested
lektra.event.EventType.OnTextSelectionContextMenuRequested
```

### Functions

- `lektra.event.register(EventType, callback) -> integer`
  Register a persistent callback. Returns a handle for `unregister`.
  Callback receives the `Lektra` instance as a light userdata (rarely needed).

- `lektra.event.unregister(EventType, handle)`
  Remove a callback by the handle returned from `register`.

- `lektra.event.once(EventType, callback) -> integer`
  Register a one-shot callback. Automatically removed after first dispatch.

- `lektra.event.count(event_name: string) -> integer`
  Number of registered callbacks for a named event (takes a string name here).

### Example

```lua
local ET = lektra.event.EventType

lektra.event.once(ET.OnAppReady, function()
    lektra.ui.message("Lektra is ready", 2)
end)

lektra.event.register(ET.OnPageChanged, function()
    local v = lektra.view.current()
    if v then
        lektra.ui.message("Page " .. v:pageno(), 1)
    end
end)
```

> **Note:** Per-view events registered with `view:register(...)` use **string** event names,
> not integer `EventType` constants. The two APIs are separate.

---

## lektra.bookmarks

Read the global bookmark list.

### Types

Each entry returned by `list()` is a table with:

| Field | Type | Description |
|---|---|---|
| `id` | `string` | Unique bookmark ID. |
| `file_path` | `string` | Path to the bookmarked file. |
| `pageno` | `integer` | 1-based page number. |
| `x` | `number` | X coordinate on the page. |
| `y` | `number` | Y coordinate on the page. |
| `created` | `string` | Creation timestamp (human-readable). |

### Functions

- `lektra.bookmarks.list() -> Bookmark[]`
  Return all bookmarks across all documents.

### Example

```lua
for _, bm in ipairs(lektra.bookmarks.list()) do
    print(bm.file_path, bm.pageno)
end
```

> **Tip:** To add or remove bookmarks use `lektra.cmd.execute("bookmark_add")` and
> `lektra.cmd.execute("bookmark_remove")`.

---

## lektra.tabs

Tab management.

### Tab object methods

`lektra.tabs.current()` and similar functions return a **Tab** object with these methods:

| Method | Returns | Description |
|---|---|---|
| `tab:id()` | `integer \| nil` | Stable tab ID, or `nil` if invalid. |
| `tab:index()` | `integer \| nil` | Current positional index (0-based), or `nil`. |
| `tab:title()` | `string \| nil` | Tab title text, or `nil`. |
| `tab:view()` | `View \| nil` | The primary view in this tab, or `nil`. |
| `tab:close()` | — | Close this tab. |

> **Note:** `tab:index()` is positional and can become stale if tabs are reordered or
> closed. Use `tab:id()` for stable identity.

### Module functions

| Function | Returns | Description |
|---|---|---|
| `lektra.tabs.current()` | `Tab \| nil` | Currently active tab. |
| `lektra.tabs.list()` | `Tab[]` | All open tabs as Tab objects. |
| `lektra.tabs.count()` | `integer` | Number of open tabs. |
| `lektra.tabs.get_id(index)` | `integer \| nil` | Tab ID for a given positional index. |
| `lektra.tabs.goto(index)` | — | Switch to tab at positional index. |
| `lektra.tabs.close(index?)` | — | Close tab at index (current tab if omitted). |
| `lektra.tabs.next()` | — | Switch to next tab. |
| `lektra.tabs.prev()` | — | Switch to previous tab. |
| `lektra.tabs.first()` | — | Switch to first tab. |
| `lektra.tabs.last()` | — | Switch to last tab. |
| `lektra.tabs.move_left()` | — | Move current tab left. |
| `lektra.tabs.move_right()` | — | Move current tab right. |

### Example

```lua
-- Print all open tab titles
for _, tab in ipairs(lektra.tabs.list()) do
    print(tab:index(), tab:title())
end

-- Get the view from the current tab
local tab = lektra.tabs.current()
if tab then
    local v = tab:view()
    if v then
        print(v:file_path())
    end
end
```

---

## lektra.keymap

Keyboard binding helpers.

### Functions

- `lektra.keymap.set(command, keys: string[])`
  Set the key sequence(s) for a command. `keys` is a table of key strings.

- `lektra.keymap.unset(command)`
  Remove all key bindings for a command.

- `lektra.keymap.get(command) -> string[]`
  Return the current key bindings for a command.

### Example

```lua
lektra.keymap.set("zoom_in",  { "Ctrl++", "=" })
lektra.keymap.set("zoom_out", { "Ctrl+-", "-" })
print(lektra.keymap.get("zoom_in")[1])  -- "Ctrl++"
```

---

## lektra.mousemap

Mouse binding helpers.

### Functions

- `lektra.mousemap.set(action, trigger: string)`
  Bind an action to a mouse trigger string (e.g. `"Ctrl+LeftButton"`).

- `lektra.mousemap.unset(action)`
  Remove the mouse binding for an action.

- `lektra.mousemap.get(action) -> string`
  Return the current trigger for an action.

### Default action names

| Action | Description |
|---|---|
| `pan` | Pan / scroll the document. |
| `preview` | Show hover preview. |
| `portal` | Create or focus a portal. |
| `synctex_jump` | SyncTeX source jump. |

### Example

```lua
lektra.mousemap.set("pan", "Alt+LeftButton")
```

---

## lektra.opt

Read and write configuration options at runtime. All options are under
`lektra.opt.<section>.<key>`.

### Example

```lua
lektra.opt.search.case_sensitive = true
lektra.opt.rendering.invert_color = false
print(lektra.opt.zoom.default)
```

Enum tables available under `lektra.opt`:

- `lektra.opt.FitMode` — `WIDTH`, `HEIGHT`, `WINDOW`
- `lektra.opt.LayoutMode` — `SINGLE`, `HORIZONTAL`, `VERTICAL`, `BOOK`
- `lektra.opt.MouseButton` — `LEFT`, `RIGHT`, `MIDDLE`

---

## lektra.timer

Lua-accessible `QTimer` wrapper. Timers are parented to the main window, so they are
always cleaned up on shutdown even if `destroy()` is never called. The `__gc` metamethod
additionally releases the timer and its callback as soon as the Lua userdata is
garbage-collected.

### Constructor

- `lektra.timer.new(interval_ms: integer, callback: function, single_shot?: boolean) -> Timer`
  Create a new timer. `interval_ms` is the period in milliseconds. `single_shot` defaults
  to `false` (repeating). The timer is **not** started automatically — call `t:start()`.

### Timer methods

| Method | Returns | Description |
|---|---|---|
| `t:start()` | — | Start (or restart) the timer. |
| `t:stop()` | — | Stop the timer without destroying it. |
| `t:set_interval(ms)` | — | Change the interval. Takes effect on the next `start()`. |
| `t:set_single_shot(b)` | — | Set whether the timer fires once (`true`) or repeats (`false`). |
| `t:is_active()` | `boolean` | Whether the timer is currently running. |
| `t:is_single_shot()` | `boolean` | Whether the timer is configured as single-shot. |
| `t:interval()` | `integer` | Current interval in milliseconds. |
| `t:destroy()` | — | Stop and delete the timer immediately. Safe to call multiple times. |

### Example

```lua
-- Repeating timer: print the current page every 5 seconds
local t = lektra.timer.new(5000, function()
    local v = lektra.view.current()
    if v then
        print("Current page:", v:pageno())
    end
end)
t:start()

-- One-shot: show a message 2 seconds after a file opens
local ET = lektra.event.EventType
lektra.event.register(ET.OnFileOpen, function()
    local reminder = lektra.timer.new(2000, function()
        lektra.ui.message("Don't forget to bookmark your page!", 3)
    end, true)
    reminder:start()
end)

-- Stop and clean up eagerly
t:stop()
t:destroy()
```

---

## lektra.utils

General utilities.

### Functions

- `lektra.utils.print(...)`
  Pretty-print any values to stdout, including nested tables.

- `lektra.utils.open_url(url)`
  Open a URL in the system default browser.

- `lektra.utils.platform() -> string`
  Returns the current platform: `"linux"`, `"windows"`, or `"macos"`.

### Example

```lua
if lektra.utils.platform() == "linux" then
    lektra.utils.open_url("https://example.com")
end

lektra.utils.print({ key = "value", nested = { 1, 2, 3 } })
```

---

## Event reference

### Global events (`lektra.event`)

Use `lektra.event.EventType.<Name>` as the first argument to `register`, `unregister`, and `once`.

| Name | Fired when |
|---|---|
| `OnAppReady` | Application fully initialized. |
| `OnReady` | A document view is ready. |
| `OnFileOpen` | A file is opened in a view. |
| `OnFileClose` | A file is closed. |
| `OnPageChanged` | Current page changes. |
| `OnZoomChanged` | Zoom level changes. |
| `OnLinkClicked` | A link in the document is clicked. |
| `OnTextSelected` | Text selection changes. |
| `OnTabChanged` | The active tab changes. |
| `OnSearchStarted` | A search is started. |
| `OnSearchFinished` | A search completes. |
| `OnSearchCancelled` | A search is cancelled. |
| `OnAnnotationAdded` | An annotation is added. |
| `OnAnnotationRemoved` | An annotation is removed. |
| `OnRegionSelectionContextMenuRequested` | Region-selection context menu opens. |
| `OnTextSelectionContextMenuRequested` | Text-selection context menu opens. |

### Per-view events (`view:register`)

These use **string names**, not `EventType` constants.

| Name | Fired when |
|---|---|
| `"OnPageChanged"` | Page changes in this view. |
| `"OnZoomChanged"` | Zoom changes in this view. |
| `"OnFileOpen"` | File opens in this view. |
| `"OnFileClose"` | File closes in this view. |
| `"OnTextSelected"` | Text is selected in this view. |
| `"OnLinkClicked"` | Link clicked in this view. |
| `"OnSearchStarted"` | Search started in this view. |
| `"OnSearchFinished"` | Search finished in this view. |
| `"OnSearchCancelled"` | Search cancelled in this view. |

---

## Full example — context menu with page-change tracking

```lua
local ET = lektra.event.EventType
local registered = {}

local function attach(view)
    if not view then return end
    local id = view:id()
    if registered[id] then return end
    registered[id] = true

    view:register_context_menu("TextSelection", function(v, menu)
        menu:add_item("Copy Uppercase", function()
            local text = v:selection_text(false)
            lektra.utils.print(text:upper())
        end)
    end)

    view:register("OnPageChanged", function(v)
        lektra.ui.message("Page " .. v:pageno() .. " / " .. v:page_count(), 1)
    end)
end

lektra.event.once(ET.OnAppReady, function()
    local tab = lektra.tabs.current()
    if tab then attach(tab:view()) end
end)

lektra.event.register(ET.OnTabChanged, function()
    attach(lektra.view.current())
end)

lektra.event.register(ET.OnFileOpen, function()
    attach(lektra.view.current())
end)
```
