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
| `view:region_select(callback)` | — | Enter rubber-band selection mode. When the user draws a rectangle the callback fires with `{ x, y, w, h }` (scene coordinates) and the view returns to normal. Bypasses the default context menu. |
| `view:narrow_to_region()` | — | Enter rubber-band selection mode and narrow the view to the drawn rectangle. If the view is already narrowed, widens it instead (toggle). |
| `view:narrow_to_pages(start, end)` | — | Narrow the view to an inclusive 1-indexed page range (e.g. `5, 10`). Scrolling, rendering focus, and search are all limited to pages in the range. |
| `view:widen_region()` | — | Exit narrow mode and restore the full document view. No-op if not narrowed. |
| `view:narrow_to_section(title)` | — | Narrow to the section matching `title` from the document outline (exact then substring match). The region extends from the section's start page through the page where the next non-descendant section begins, so overflow content is always included. Requires the document to have an outline (embedded, generated, or loaded). |
| `view:is_narrowed()` | `boolean` | Whether the view is currently in narrow mode. |
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
| `view:export_pages(names, pages?, opts?)` | `string[]?, string?` | Save pages to files: pictures (png, jpg, webp, bmp, tif; one per page, `%d` in the name is the page number), or pdf, svg, txt, html, cbz, docx, odt. `pages`: a number, a list, or text like `"1-5,8"`, `"all"`, `"odd"` (default: current page). `opts`: `dpi` (default 150), `overwrite` (default `false`), `split` (make one file per page instead of one file, for pdf, txt, html, ...). Returns the files written, or `nil` and a reason. |
| `view:detach_to_window()` | `boolean` | Move this split into a window of its own, at the page it shows (zoom and position are not kept) and close it here. `false` if it is the only split of its tab. |
| `view:detach_to_tab()` | `boolean` | Move this split into a tab of its own, at the page it shows. `false` if it is the only split of its tab. |
| `view:opt()` | `ViewOptions` | The options of this view only (see `lektra.opt`). |
| `view:opt_reset(section?)` | `boolean` | Put the local options of this view (or one section of them) back to what a new view would have. |
| `view:history_stack()` | `HistoryLocation[], integer` | The jump locations of the view, oldest first, as `{ pageno, x, y }` (page numbers are 1-based), and the position of the current one in the list (0 if none). What `history_back` and `history_forward` walk through. |
| `view:set_mark(char)` | `boolean` | Mark the current location. `a`-`z` belong to this view, `A`-`Z` are global. Same as the `mark_set` command. |
| `view:goto_mark(char)` | `boolean` | Go to a mark; `false` if there is no such mark. A global mark can switch to another view. |
| `view:image_metadata()` | `table?, string?` | For an image: `{ width, height, format, animated, frames, dpi_x, dpi_y, exif }` (`dpi_*` only if the file stores it, `exif` maps tag names to values). `nil` and a message for other documents. |
| `view:scene_to_page(x, y)` | `pageno, page_x, page_y` | A point of the canvas as a point of a page, in page points from the top left (like `page_size` and annotations). `nil` if it is not on a page. |
| `view:page_to_scene(pageno, x, y)` | `scene_x, scene_y` | The opposite. Works for the pages that are loaded (the visible ones and their neighbours); `nil` and a message for another page. |
| `view:annotations([page])` | `Annotation[]?, string?` | The annotations of a page (1-based), or of the whole document without `page` (PDF only). Each is a table `{ id, page, type, x, y, w, h, color, opacity, comment }`; `type` is `"highlight"`, `"rect"`, `"note"` or `"other"`; a highlight also has `text` (the text under it) and `rects` (one `{ x, y, w, h }` per line). Positions are page points, with the origin at the top left. |
| `view:add_highlight(page, rects, opts?)` | `integer?, string?` | Highlight one `{ x, y, w, h }` or a list of them (one per line) on `page`. `opts`: `color` (`"#rrggbb"`, a colour name), `comment`. Returns the id of the annotation. Can be undone. |
| `view:add_note(page, x, y, text)` | `integer?, string?` | Add a sticky note at `x, y`. Returns the id. Can be undone. |
| `view:add_rect(page, rect, opts?)` | `integer?, string?` | Add a rectangle `{ x, y, w, h }`. `opts`: `comment`. Returns the id. Can be undone. |
| `view:set_annotation(page, id, opts)` | `boolean` | Change `comment` and/or `color` of an annotation. `false` if there is no such annotation. Cannot be undone. |
| `view:remove_annotation(page, id)` | `boolean` | Remove an annotation. `false` if there is no such annotation. Can be undone. |
| `view:has_selection()` | `boolean` | Whether text is selected. |
| `view:selection_text(formatted)` | `string` | Selected text. |
| `view:clear_selection()` | — | Clear the current selection. |
| `view:search(query, regex?)` | — | Search the document. |
| `view:search_below(query, regex?)` | — | Search from the current page forward (inclusive). Narrow region still takes precedence. |
| `view:search_above(query, regex?)` | — | Search from page 0 through the current page (inclusive). Narrow region still takes precedence. |
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

#### Narrow to region example

```lua
-- Toggle narrow mode on the current view
lektra.cmd.register("toggle_narrow", function()
    local v = lektra.view.current()
    if not v then return end
    if v:is_narrowed() then
        v:wide_region()
    else
        v:narrow_to_region()
    end
end, "Narrow view to selected region (toggle)")
```

#### Region selection example

```lua
-- Ask the user to draw a rectangle, then report its dimensions
local v = lektra.view.current()
if v then
    v:region_select(function(area)
        lektra.ui.message(
            string.format("Region: x=%.1f y=%.1f  %dx%d",
                area.x, area.y, area.w, area.h), 3)
    end)
end

-- Use region_select to drive a custom copy-at-DPI workflow from a command
lektra.cmd.register("copy_region_600dpi", function()
    local v = lektra.view.current()
    if not v then return end
    v:region_select(function(area)
        -- area.x/y/w/h are in scene coordinates
        lektra.ui.message("Region captured — implement custom handling here", 2)
    end)
end, "Copy selected region at 600 DPI (example)")
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

- `lektra.ui.file_dialog(mode?, options?) -> path | paths | nil, filter?`
  File dialog. `mode` is `"open"` (one file, the default), `"open_multiple"` (a list of files),
  `"save"` (a file name) or `"directory"` (a folder). The options can also be the only
  argument, with `mode` in them. Returns `nil` if cancelled; the second value is the filter
  that was selected (not for `"directory"`).
  `options` table (all optional): `title`, `directory` (where to start; `default_path` is the
  older name), `filename` (suggested for `"save"`, selected for `"open"`), `filters` (a list
  such as `{"PDF (*.pdf)", "All files (*)"}`, or one text with `;;` between them),
  `selected_filter` (the one to start with), `default_suffix` (`"save"`: added to a name typed
  without an extension), `confirm_overwrite` (`"save"`: ask before replacing; default `true`).

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
local file = lektra.ui.file_dialog("open", { filters = { "PDF (*.pdf)", "All files (*)" } })
if file then
    lektra.ui.message("Opened: " .. file, 3)
end

-- several files, or a folder
local files = lektra.ui.file_dialog("open_multiple", { title = "Pick some PDFs", filters = "PDF (*.pdf)" })
local folder = lektra.ui.file_dialog("directory", { directory = "/home" })

-- a file name to save to
local target = lektra.ui.file_dialog({ mode = "save", filename = "notes.txt", default_suffix = "txt" })
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
| `lektra.tabs.split_to_tabs(indices?)` | `integer` | Move the splits of tabs into tabs of their own, keeping the first split of each in place. Defaults to the selected tabs, else the current tab. Returns how many splits were moved. Also named `split_out`. |
| `lektra.tabs.split_to_windows(indices?)` | `integer` | The same, but each split gets a window of its own, opened at the page it showed (the zoom and position are not kept). |

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

**Global and local options** work as in Vim. Each view has its own copy of the view
options, and a new split starts with a copy of the one it was made from.

| Lua | Vim | Changes |
|---|---|---|
| `lektra.opt.<section>.<key> = v` | `:set` | the global default and the current view |
| `lektra.opt_global.<section>.<key> = v` | `:setglobal` | only the global default |
| `view:opt().<section>.<key> = v` | `:setlocal` | only that view |
| `view:opt_reset([section])` | `:setlocal opt<` | puts that view back to the defaults |

`view:opt_reset()` gives the view what a new view of its file type would start with:
the global options, with the `[filetype.<type>]` overrides on top.

A **color** option takes a string, `"#RRGGBBAA"` or `"#RRGGBB"` (opaque), as in
`config.toml`, or an integer `0xRRGGBBAA`. Reading one returns the integer.
For example `lektra.opt.page.bg = "#1e1e2e"` or `lektra.opt.window.accent = 0x3DAEE9FF`.

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

## lektra.job

Run a command in the background without freezing the window. `lektra.job.async` returns at
once; the callback is called later, when the command has finished. Not available to the
LLM assistant's scripts.

- `lektra.job.async(command, [options], callback) -> Job`
  `command` is a string (run by the shell, `sh -c`) or a list `{"program", "arg", ...}` (run
  directly, nothing to quote). `callback(result)` can also be given as `options.on_done`.

### Options

| Option | Description |
|---|---|
| `cwd` | Folder to run in. |
| `env` | Table of environment variables to add or change. |
| `stdin` | Text given to the command's standard input (closed afterwards; without it, closed at once). |
| `timeout` | Seconds after which the command is stopped. |
| `on_stdout`, `on_stderr` | `function(chunk)` called with the output as it arrives. |
| `on_done` | Same as the `callback` argument. |

### Result

`{ ok, code, stdout, stderr, timed_out, cancelled, error }` — `ok` is true only for exit code 0;
`code` is -1 if the command could not start or was killed; `error` says why when it did not
simply exit ("timed out", "cancelled", "could not start: ...").

### Job methods

| Method | Description |
|---|---|
| `j:cancel()` | Stop the command and what it started. The callback is still called, with `cancelled = true`. |
| `j:running()` | Whether it is still running. |
| `j:pid()` | Process id while running, otherwise `nil`. |

Jobs still running when Lektra quits are killed and their callbacks are not called.

### Example

```lua
lektra.job.async({"tesseract", "page.png", "out", "-l", "eng"}, { timeout = 60 }, function(r)
    if r.ok then
        lektra.ui.message("OCR done")
    else
        lektra.ui.message("OCR failed: " .. (r.error or r.stderr))
    end
end)
```

---

## lektra.paths

The folders Lektra keeps its files in. Each function returns an absolute path without a
trailing slash, and creates the folder if it is missing. Not available to the LLM
assistant's scripts.

| Function | Description |
|---|---|
| `lektra.paths.config()` | Configuration files (`init.lua`, `config.toml`). |
| `lektra.paths.data()` | Data Lektra keeps (history, bookmarks, sessions); a good place for a script's own files. |
| `lektra.paths.cache()` | Files that can be thrown away and made again. |

```lua
local notes = lektra.paths.data() .. "/my-notes.txt"
```

---

## lektra.statusbar

Pieces of text a script puts in the statusbar. A segment is a module like `page` or `zoom`:
it goes where `statusbar.layout` names it (`"mysegment"`, or with options such as
`{ module = "mysegment", stretch = 1 }`), or to the right end if the layout does not.
Not available to the LLM assistant's scripts.

| Function | Description |
|---|---|
| `lektra.statusbar.register(name, provider, opts?)` | Add a segment. `provider` is a `function(view)` called with the current view (or `nil`) when the statusbar changes (page, zoom, file, mode...) and every `opts.interval` seconds if given. It returns the text, a table `{ text =, tooltip = }`, or `nil` to hide the segment. A string instead of a function is a fixed text. `opts`: `interval` (seconds, at least 0.1), `tooltip`, `on_click` (`function()`). Registering a name again replaces it. |
| `lektra.statusbar.set(name, text, opts?)` | Show `text` in a segment, without a provider (use it from callbacks, e.g. job progress). `nil` or `""` hides it. `opts`: `tooltip`, `on_click`. |
| `lektra.statusbar.update(name?)` | Ask the provider of a segment, or of all segments, again now. |
| `lektra.statusbar.unregister(name)` | Remove a segment. Returns `false` if there was none. |

The built-in names (`session`, `filename`, `page`, `zoom`, `progress`, `mode`, `portal`,
`narrow`) cannot be used. A layout may name a segment before the script has registered it:
it is shown once it exists. An error in a provider is printed once and hides the segment.

```lua
lektra.statusbar.register("words", function(view)
    if not view then return nil end
    local n = 0
    for _ in view:page_text():gmatch("%S+") do n = n + 1 end
    return n .. " words"
end, { tooltip = "Words on this page" })

lektra.statusbar.register("clock", function() return os.date("%H:%M") end, { interval = 30 })
```

---

## lektra.async

Write several slow steps in a row without nesting callbacks. A function run with
`lektra.async` is a coroutine: at an `await` it pauses, and the rest runs later, from the
event loop, while the window stays responsive. Not available to the LLM assistant's scripts.

| Function | Description |
|---|---|
| `lektra.async(fn, ...)` | Run `fn(...)` as a coroutine. It starts at once and goes on until its first wait; returns the coroutine. An error inside is printed and ends only that task. Tasks can run side by side. |
| `lektra.job.await(command, opts?)` | `lektra.job.async`, waited for: returns the result table (`ok`, `code`, `stdout`, `stderr`, ...). |
| `lektra.sleep(seconds)` | Wait. |
| `lektra.await(starter)` | Wait for any callback-based function. `starter(resume)` starts it; the task continues when `resume(...)` is called and `await` returns what it was given. Only the first call of `resume` counts. |

`await` and `sleep` raise an error outside `lektra.async`. Dialogs such as `lektra.ui.file_dialog`
already wait by themselves and can be used in a task as they are.

```lua
lektra.async(function()
    local r = lektra.job.await({"tesseract", png, base, "-l", "eng"}, { timeout = 120 })
    if not r.ok then
        lektra.ui.message("OCR failed: " .. (r.error or r.stderr))
        return
    end
    lektra.sleep(1)
    lektra.ui.message("OCR done")
end)

-- any callback API:
local area = lektra.await(function(resume) view:region_select(resume) end)
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
| `OnSynctexJumpRequested` | A SyncTeX jump from the PDF to the LaTeX source is requested (see below). |

### `OnSynctexJumpRequested`

Fired when the user asks to jump from a SyncTeX PDF to its LaTeX source (by default
Shift + left click on the page; the `synctex_jump` mouse binding). The callback gets
`(view, source_file, line, col)`:

| Argument | Description |
|---|---|
| `view` | The view that was clicked. |
| `source_file` | The source file, as SyncTeX recorded it. |
| `line` | The line in that file. |
| `col` | The column, or `-1` if SyncTeX does not know it. |

It is fired once for each source location found, just before the editor command
(`synctex.editor_command`) runs, which still happens. It is a global event: register it
with `lektra.event.register`, not `view:register`. Only available when Lektra is built with
SyncTeX support.

```lua
lektra.event.register("OnSynctexJumpRequested", function(view, source_file, line, col)
    lektra.ui.message(("Jumping to %s:%d"):format(source_file, line))
end)
```

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
