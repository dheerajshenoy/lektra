# lektra

> [!NOTE]
> This is Work in Progress and is subject to change as the API evolves. The documentation will be updated regularly to reflect the latest state of the API.

## lektra.keymap

- lektra.keymap.set(name, value)
- lektra.keymap.unset(name)

---

## lektra.mousemap

- lektra.mousemap.set(name, value)
- lektra.mousemap.unset(name)

---

## lektra.enums

### lektra.LayoutMode

- VERTICAL
- HORIZONTAL
- BOOK
- SINGLE

### lektra.MouseButton

- LEFT
- RIGHT
- MIDDLE

### lektra.FitMode

- WIDTH
- HEIGHT
- WINDOW

---

## lektra.opt

### lektra.opt.page
* **lektra.opt.page.bg**: Background color of the page.
* **lektra.opt.page.fg**: Foreground (text) color of the page.

### lektra.opt.synctex
* **lektra.opt.synctex.enabled**: Boolean to enable or disable SyncTeX support.
* **lektra.opt.synctex.editor_command**: String representing the command used to open the external editor.

### lektra.opt.ui
* **lektra.opt.ui.theme**: String for the UI theme name.
* **lektra.opt.ui.font_family**: String for the interface font.
* **lektra.opt.ui.font_size**: Integer for the interface font size.
* **lektra.opt.ui.icon_theme**: String for the icon set used in the UI.

### lektra.opt.session
* **lektra.opt.session.restore**: Boolean to toggle restoring the previous session on startup.
* **lektra.opt.session.save_interval**: Integer (seconds) for how often the session state is autosaved.

### lektra.opt.statusbar
* **lektra.opt.statusbar.visible**: Boolean to toggle the visibility of the status bar.
* **lektra.opt.statusbar.padding**: Table `[top, right, bottom, left]` for status bar spacing.

### lektra.opt.zoom
* **lektra.opt.zoom.factor**: Float for the increment used when zooming in/out.
* **lektra.opt.zoom.level**: Float for the current zoom percentage.
* **lektra.opt.zoom.anchor_to_mouse**: Boolean to zoom relative to the cursor position.

### lektra.opt.selection
* **lektra.opt.selection.color**: Integer (Hex) for the text selection highlight color.
* **lektra.opt.selection.copy_on_select**: Boolean to automatically copy text to the clipboard when selected.
* **lektra.opt.selection.drag_threshold**: Integer for minimum distance before a click becomes a drag.

### lektra.opt.split
* **lektra.opt.split.dim_inactive**: Boolean to dim the non-focused split view.
* **lektra.opt.split.dim_inactive_opacity**: Float (0.0-1.0) for the dimming intensity.
* **lektra.opt.split.focus_follows_mouse**: Boolean to change focus as the cursor moves over a split.
* **lektra.opt.split.mouse_follows_focus**: Boolean to warp the cursor to the focused split.

### lektra.opt.scrollbars
* **lektra.opt.scrollbars.vertical**: Boolean to show the vertical scrollbar.
* **lektra.opt.scrollbars.horizontal**: Boolean to show the horizontal scrollbar.
* **lektra.opt.scrollbars.size**: Integer for the width/thickness of the scrollbars.
* **lektra.opt.scrollbars.auto_hide**: Boolean to hide scrollbars when not in use.
* **lektra.opt.scrollbars.hide_timeout**: Float (seconds) before the scrollbar disappears.
* **lektra.opt.scrollbars.search_hits**: Boolean to show search result markers on the scrollbar.

### lektra.opt.jump_marker
* **lektra.opt.jump_marker.enabled**: Boolean to show a visual marker when jumping to a link/page.
* **lektra.opt.jump_marker.color**: Integer (Hex) for the marker color.
* **lektra.opt.jump_marker.fade_duration**: Float (seconds) for the marker animation.

### lektra.opt.links
* **lektra.opt.links.enabled**: Boolean to enable clickable links.
* **lektra.opt.links.detect_urls**: Boolean to automatically parse plain text URLs.
* **lektra.opt.links.boundary**: Boolean to highlight link boundaries.
* **lektra.opt.links.url_regex**: String for custom URL detection patterns.

### lektra.opt.link_hints
* **lektra.opt.link_hints.fg**: Integer (Hex) for the hint label text color.
* **lektra.opt.link_hints.bg**: Integer (Hex) for the hint label background color.
* **lektra.opt.link_hints.size**: Float for the font size of the hint labels.

### lektra.opt.tabs
* **lektra.opt.tabs.visible**: Boolean to show the tab bar.
* **lektra.opt.tabs.auto_hide**: Boolean to hide the tab bar if only one tab is open.
* **lektra.opt.tabs.closable**: Boolean to show close buttons on tabs.
* **lektra.opt.tabs.movable**: Boolean to allow reordering tabs by dragging.
* **lektra.opt.tabs.full_path**: Boolean to show the full file path in the tab title.
* **lektra.opt.tabs.lazy_load**: Boolean to defer loading tab content until the tab is selected.

### lektra.opt.picker
- **lektra.opt.picker.width**: Width in pixels (>1) or relative (0.0-1.0).
- **lektra.opt.picker.height**: Height in pixels (>1) or relative (0.0-1.0).
- **lektra.opt.picker.border**: Boolean to show a border around the picker.
- **lektra.opt.picker.alternating_row_color**: Boolean to enable zebra-striping in lists.
- **lektra.opt.picker.shadow.enabled**: Boolean to enable the picker shadow.
- **lektra.opt.picker.shadow.blur_radius**: Integer for shadow blur radius.
- **lektra.opt.picker.shadow.offset_x**: Integer for horizontal shadow offset.
- **lektra.opt.picker.shadow.offset_y**: Integer for vertical shadow offset.
- **lektra.opt.picker.shadow.opacity**: Integer (0-255) for shadow transparency.

### lektra.opt.outline
- **lektra.opt.outline.indent_width**: Integer for tree indentation.
- **lektra.opt.outline.show_page_number**: Boolean to display page numbers in the outline.
- **lektra.opt.outline.flat_menu**: Boolean to show a flat list instead of a hierarchy.

### lektra.opt.highlight_search
- **lektra.opt.highlight_search.flat_menu**: Boolean to show search results in a flat menu.

### lektra.opt.command_palette
- **lektra.opt.command_palette.placeholder_text**: String displayed in the empty input field.
- **lektra.opt.command_palette.vscrollbar**: Boolean to show the vertical scrollbar.
- **lektra.opt.command_palette.show_shortcuts**: Boolean to show keybindings next to commands.
- **lektra.opt.command_palette.description**: Boolean to show extended command descriptions.

### lektra.opt.rendering
- **lektra.opt.rendering.dpr**: Float or table for Device Pixel Ratio.
- **lektra.opt.rendering.antialiasing**: Boolean to toggle general antialiasing.
- **lektra.opt.rendering.antialiasing_bits**: Integer for antialiasing quality.
- **lektra.opt.rendering.text_antialiasing**: Boolean to toggle text-specific smoothing.
- **lektra.opt.rendering.smooth_pixmap_transform**: Boolean for high-quality image scaling.
- **lektra.opt.rendering.backend**: String choice: `"auto"`, `"raster"`, or `"opengl"`.

### lektra.opt.behavior
- **lektra.opt.behavior.confirm_on_quit**: Boolean to ask before closing.
- **lektra.opt.behavior.undo_limit**: Integer for max undo steps.
- **lektra.opt.behavior.cache_pages**: Integer for number of pages kept in memory.
- **lektra.opt.behavior.preload_pages**: Integer for how many pages to render ahead.
- **lektra.opt.behavior.auto_reload**: Boolean to reload on file change.
- **lektra.opt.behavior.invert_mode**: Boolean for dark/invert mode.
- **lektra.opt.behavior.dont_invert_images**: Boolean to preserve original colors in photos.
- **lektra.opt.behavior.open_last_visited**: Boolean to reopen the last file on launch.
- **lektra.opt.behavior.single_instance**: Boolean to restrict Lektra to one window.
- **lektra.opt.behavior.remember_last_visited**: Boolean to save page position.
- **lektra.opt.behavior.recent_files**: Boolean to enable the recent files list.
- **lektra.opt.behavior.num_recent_files**: Integer limit for recent files history.
- **lektra.opt.behavior.page_history_limit**: Integer for back/forward navigation history.
- **lektra.opt.behavior.initial_mode**: String for default tool (e.g., `"text_selection"`).

### lektra.opt.preview
- **lektra.opt.preview.size_ratio**: Table `{ width = 0.6, height = 0.7 }` relative to main window.
- **lektra.opt.preview.border_radius**: Integer for rounded window corners.
- **lektra.opt.preview.close_on_click_outside**: Boolean to dismiss on lost focus.
- **lektra.opt.preview.opacity**: Float (0.0-1.0) for window transparency.

### lektra.opt.misc
- **lektra.opt.misc.color_dialog_colors**: List of strings (Hex ARGB) for the color picker palette.

---

## lektra.api
Core functions for document management and command registration.

* **`open(file, [docnr], [callback], [options])`**: Opens a file.
  * `docid`: Optional integer of target document to open in (defaults to current).
  * `callback`: `function(view)` called once the view is ready.
  * `options`: See [OpenFileOptions](#openfileoptions) table.
* **`close([docnr])`**: Closes the specified document (defaults to current).
* **`hsplit([docnr])`**: Splits the specified view horizontally.
* **`vsplit([docnr])`**: Splits the specified view vertically.
* **`docnr()`**: Returns the `ID` (string/int) of current document.
  * `callback`: `function(args)` receiving a table of command-line arguments.

---

## lektra.cmd

* **`register(name, callback)`**: Adds a custom command to Lektra.
* **`unregister(name)`**: Removes a previously registered command.
* **`execute(name, [args])`**: Executes a registered command with optional arguments.

## lektra.ui
Functions for interacting with the user interface.

* **`show_message(title, message, [type])`**: Displays a popup dialog.
  * `type`: `"info"` (default), `"warning"`, or `"error"`.
* **`input(title, prompt, callback)`**: Opens an input prompt.
  * `callback`: `function(input)` receiving the string entered by the user.
* **`picker(title, items, callback, [opts])`**: Shows a selection picker.
  * `items`: List of strings or tables with `label` and `value`.
  * `callback`: `function(selected)` receiving the chosen item.
  * `opts`: See [PickerOptions](#pickeroptions) table.

---

## lektra.event
Subscription-based event system.

### `lektra.event.on(event_name, callback)`
| Event Name | Description |
| :--- | :--- |
| **`OnReady`** | Emitted when the application has finished loading and the Lua environment is live. |
| **`OnDocumentOpen`** | Emitted after a document is successfully opened. Callback receives `function(docid, filepath)`. |
| **`OnDocumentClose`** | Emitted after a document is closed. Callback receives `function(docid)`. |
| **`OnPageChange`** | Emitted when the user navigates to a different page. Callback receives `function(docid, page_number)`. |
| **`OnZoomChange`** | Emitted when the zoom level changes. Callback receives `function(docid, zoom_level)`. |
| **`OnLinkClick`** | Emitted when a link is clicked. Callback receives `function(docid, link_url)`. |
| **`OnError`** | Emitted when an error occurs. Callback receives `function(error_message)`. |


---

## Tables

### `OpenFileOptions`
Pass this table to `lektra.api.open` to control how the document is rendered.

| Field | Type | Description |
| :--- | :--- | :--- |
| **`hsplit`** | `bool` | If true, opens the file in a new horizontal split. |
| **`vsplit`** | `bool` | If true, opens the file in a new vertical split. |
| **`new_window`**| `bool` | If true, opens the file in a separate OS window. |
| **`dwim`** | `bool` | "Do What I Mean." Automatically decides the split/window logic based on current layout. |

### `PickerOptions`

Pass this table to `lektra.ui.picker` to customize the appearance and behavior of the picker dialog.

| Field | Type | Description |
| :--- | :--- | :--- |
| **`width`** | `int` or `float` | Width in pixels (>1) or relative (0.0-1.0) to main window. |
| **`height`** | `int` or `float` | Height in pixels (>1) or relative (0.0-1.0) to main window. |
| **`border` | `bool` | Whether to show a border around the picker. |
| **`alternating_row_color`** | `bool` | Whether to enable zebra-striping in the item list. |
| **`shadow`** | `table` | Shadow configuration with fields: `enabled` (`bool`), `blur_radius` (`int`), `offset_x` (`int`), `offset_y` (`int`), and `opacity` (`int`, 0-255). |
| **`vscrollbar`** | `bool` | Whether to show a vertical scrollbar when items exceed the visible area. |
| **`flat_menu`** | `bool` | Whether to display items in a flat list instead of a hierarchical tree. |
---
