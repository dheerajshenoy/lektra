# LEKTRA CHANGELOG

## 0.6.9

### Features

- Absolute vs Relative search hit jump
- Thumbnail panel for quick navigation and visual overview of the document pages. The thumbnail panel can be toggled with
the `thumbnail_panel` command and will show a scrollable list of page thumbnails on the side of the window.
Clicking on a thumbnail will navigate to that page in the main view.

- Default keybindings added for the following:
  - `split_horizontal` => `Ctrl+W,s`
  - `split_vertical` => `Ctrl+W,v`
  - `split_focus_left` => `Ctrl+W,h`
  - `split_focus_right` => `Ctrl+W,l`
  - `split_focus_up` => `Ctrl+W,k`
  - `split_focus_down` => `Ctrl+W,j`
  - `split_close` => `Ctrl+W,c`

### Config Options

- `[thumbnail_panel]` section for thumbnail panel related settings
    - `show_page_number` (bool): Whether to show page numbers on the thumbnails
    - `panel_width` (float): Width of the thumbnail panel as a ratio of the main window width (e.g. 0.2 for 20% of the main window width)
- Ability to define multiple keybindings for same action using [TOML array](https://toml.io/en/v1.0.0#array)

Example: 
```toml
[picker.keys]
up = [ "Ctrl+k", "Up" ]
down = [ "Ctrl+j", "Down" ]

[keybindings]
command_palette = [ ":", "Ctrl+P" ]
```

> ![NOTE]
> Notice that multiple keybindings use `[]` square brackets and not `{}` curly braces,
> which have a different meaning in [TOML](https://toml.io/en/v1.0.0#inline-table)

### Breaking Changes

- `dim_inactive` is now disabled by default (which was enabled previously out of the box)
- `confirm_on_quit` is now false by default (I feel it was annoying).

### Bug Fixes

- Don't count thumbnail view in a container as a regular split when showing the total count of the splits in a container
- Fix file properties not working
- Fix crash when trying to save file after adding annotation.

### Core Changes

## 0.6.8

### Features
- Add **progressive searching** for search results. When a search query is entered, the search results will start showing up immediately as they are found, instead of waiting for the entire search to complete before showing any results. **This is enabled by default**.
- Annotation color change is now **undo-able**.

### Config Option
- `progressive` (bool) option in the `[search]` section of the config to enable/disable progressive searching.
- `highlight_matches` (bool) option in the `[search]` section of the config to enable/disable highlighting of search matches in the document. When enabled, all search matches will be highlighted in the document, otherwise only the current search match will be highlighted.
- `load_defaults` (bool) in `[keybindings]` section to load default keybindings. This allows users to choose whether they want to start with the default keybindings or start with an empty keybindings configuration, which can be useful for users who want to fully customize their keybindings without having to override the defaults.

### Command line argument:
- `--command` to execute one or more commands directly from the command line when launching LEKTRA. This allows users to quickly perform specific actions or set up their workspace with predefined commands without having to open the command palette after launch. Multiple commands can be executed by separating them with a semicolon (e.g. `lektra --command="toggle_fullscreen;search 'hello'"` to launch in fullscreen mode and open the config file immediately).
- `--list-commands` command line argument now prints the lektra commands in sorted order
- `--version` and `--list-commands` now output to stdout instead of stderr to be more consistent with standard command line tool behavior and to allow for easier parsing of the output when using these commands in scripts or other command line workflows.
- Lektra can now accept arguments directly from the command line when using `--command` argument, allowing for more flexible and powerful command execution. For example, you can now execute a command with specific arguments directly from the command line like this: `lektra --command="page_goto 5;"` to go to page 5 immediately after launching LEKTRA.

### Commands:
- `open_config` command to open the config file in the default text editor for easy editing and customization of settings.
- `scroll_down_half_page`, `scroll_up_half_page`, commands for more precise scrolling control, allowing users to scroll by half a page in the specified direction for better navigation through documents.
- `search_from_here` to start a search from the current page and position instead of the beginning of the document, which is useful for long documents when you want to search for something that appears after your current location without having to scroll all the way back to the beginning to start the search.
- `search_cancel` to cancel an ongoing search operation

### Optimizations

### Bug Fixes

- Fix page scrolling stuttering because of the `visual_line_mode` fix
- Fix file drag and drop not working
- Fix `visual_line_mode` rect offset when zooming in/out, which caused the visual line selection rectangle to be misaligned with the text when zoom level changes.
- Fix current search highlight not updating properly when zooming in/out or resizing the window, which caused the highlight to be in the wrong position or not visible after zooming or resizing.
- Fix picker hide on mainwindow with no tabs open. It caused no shortcuts to work because the mainwindow was no longer focused after the picker was shown once and then hidden.
- For real this time, fix the statusbar 0 padding

### Core Changes

- Make different types of Picker (e.g. outline picker, search highlights picker, recent files picker, etc.) inherit **configuration** options from the common base `Picker` class **configuration** to reduce code duplication and improve maintainability of the codebase.
- Make the same inheriting structure for `Annotation` **configuration**

## 0.6.7

### Features

- `OutlinePicker` now highlights the current page in the outline for better visual feedback on the current location in the document when navigating with the outline.
- Add `--list-commands` command line argument to print the list of available commands with their descriptions to the console and exit.
- Added `fade_duration` to `[jump_marker]` config section to configure the duration of the fade out effect for the jump marker in seconds.
- Finally added `Ctrl + Mouse Wheel` zooming!
- Config option for `[zoom]` to choose whether zoom should be anchored at mouse position or center of the viewport. This can be configured with the `anchor_to_mouse` option. Otherwise, zoom will be anchored to the center of the viewport as it was before.
- Added macOS support for LEKTRA. Check out [installation](https://dheerajshenoy.github.io/lektra/installation.html) instructions for more details. (Thanks to [@budingZou](https://codeberg.org/budingZou) for the PR!)
- Added custom input dialog with text wrapping for annotation comment input, which allows for better user experience when adding comments to annotations, especially for longer comments that require more space. The dialog will automatically adjust its size based on the content and provide a more comfortable interface for entering annotation comments.

### Bug Fixes

- Fix dismiss key not working in Pickers
- Don't allow searching in unsearchable file type (e.g. DJVU)
- Fix `selection_last` not working properly.
- Handle failed to open file properly by showing an error message and closing the tab cleanly.
- Fix file not focusing when opened from the recent files picker.
- Fix viewport center anchor zoom not centering on the actual center of the viewport. (Feels more natural now)
- Now pinch-zoom respects the mouse position anchor instead of the center of the viewport
- Fix spawning multiple instances of `AboutDialog`

### Core Changes

- Add man pages for command line usage documentation `man lektra`
- Rename `panel` to `statusbar`

## 0.6.6

### Features

- Add ability to not invert images in invert mode (valid in PDF, EPUB, MOBI, XPS, FB2) . Useful for users who want to use the invert mode for dark mode but don't want their images to be inverted. This can be configured in the settings with the `dont_invert_images` option under the `[behavior]` section of the config.
- Add search history to the search bar, allowing users to easily access their previous search queries and results. The search history can be navigated using the `up` and `down` arrow keys when the search bar is focused, and selecting a previous search query from the history will re-run that search and show the results.
- Ability to "preview" links by executing a mouse binding (Alt+LeftClick by default) on a link to open a temporary view of the link target without actually navigating to it in the current view in a floating window.

<p align="center">
  <img src="./images/preview.gif" alt="Preview Demo" width="600">
</p>

- "preview" config section
```toml
[preview]
size_ratio = {0.5, 0.5} # width and height ratio of the preview window compared to the main window
border_radius = 8 # border radius of the preview window in pixels
close_on_click_outside = true # whether to close the preview window when clicking outside of it
```
- Add `preview` command, which shows the preview window (if it was created already) for the current link target. The preview window will be created on the fly when the mouse binding is executed on a link for the first time, and will be reused for subsequent previews of the same link target.

- Add mousebindings to allow users to configure mouse interactions (e.g. mouse click, double click, right click, mouse wheel, etc.) with different modifiers (e.g. Ctrl, Shift, Alt) to trigger different mouse related commands.
    **Mouse commands**:
    - `portal` - open link in portal (ctrl + click on link)
    - `syntex_jump` - jump to source location in synctex enabled PDFs

- Add `backend` option in `[rendering]` section of the config to choose between "auto", "opengl" and "raster" rendering backends. The "auto" option will choose the best available backend based on the system capabilities, with a preference for OpenGL when available for better performance.

```toml
[rendering]
backend = "auto" # "auto", "opengl", "raster"
```

- Add `--tutorial` command line argument to open the tutorial PDF file directly from the command line without having to open the command palette and search for the command. This is useful for new users who want to quickly access the tutorial file and learn how to use LEKTRA.
- Add touchpad gesture support for zooming (pinch to zoom)

### Optimizations

- **Splitting view now takes you to the same page and location in the new split instead of the first page, which provides a more seamless experience when splitting the view to reference different parts of the same document side by side.**
- Improved text selection. Text selection is now more responsive and accurate, especially for multi-columned layouts. It should now be less jumpy and more stable when selecting text across columns and pages.

### Bug Fixes

- Fix Outline picker not going to the correct page and position when selecting an entry (if target location has NaN, then default to the center of the page)
- Fix portals and preview docs appearing blurry due to not using the correct DPR for rendering
- Fix remove item 0 warning in QGraphicsScene when closing document
- Fix double memory free crash on DJVU files
- Fix link hints mode not intercepting key presses properly
- Add all supported file types to the file open dialog filter
- Fix the QPainterPath pixel max limit error when text selection is too large by using page local coordinates instead of viewport coordinates for the selection path, which should allow for larger selections without hitting the pixel limit.
- Fix opening tutorial file not working due to wrong file path
- Fix djvu code under non-djvu builds causing compilation errors
- Fix optional dependency locator in `CMakeLists.txt`

### Breaking Changes

- Removed `grid` option from `[command_palette]` section of the config as it was not implemented.

## 0.6.5

### End User Features

- Add support for language translation and localization
- Support for running shell scripts
- Support for DJVU files (limited, only basic rendering and navigation, no text selection and searching)
- New command line argument `--layout` to specify the default layout mode on file open (e.g. `--layout=book` to open in book layout mode)
- Support for adding comments to annotations in LEKTRA. (After creating an annotation, you can add a note to it by right clicking on the annotation and selecting "Comment" from the context menu. This will open a dialog where you can enter your comment. Once added, the comment will be associated with the annotation.
**Note: These comments do not exist in the undo/redo stack, which means that if you change comment of an annotation and then undo, the comment change will not be undone. This is to do with limitations in the way the annotation data is stored and handled in the current implementation, but it might be improved in the future by integrating note changes into the undo/redo stack.**
- Annotations with comments _optionally_ can show a marker (e.g. an icon) to indicate that there's a note associated with the annotation, and hovering over the annotation will show the note as a tooltip (configurable in settings).
- Annotations have an _optional_ hover glow effect to provide visual feedback when hovering over annotations, making it easier to identify and interact with them.

### Core Features

- Picker widget now supports n columns
- `CommandManager` class to manage commands instead of general hash map, which allows for better organization and management of commands, as well as support for command descriptions, useful for documentation and purpose.

### Config Option

#### `[outline]`
    - `show_page_number` (bool)
    - `indent_width` (int)
### `dim_inactive` in `[portal]` - Dim inactive for portal views too
### `description` in `[command_palette]` - Show command description in the command palette if available
### New interaction mode `initial_mode="none"` in `[behavior]`, doesn't do anything.

### `[annotations]` - annotation related settings (common for all supported annotation types)
NOTE: This is just a convenience table to increase the readability of the config, it has no effect on its own, the actual annotation type tables (e.g. `[annotations.highlight]`, `[annotations.popup]`, etc.) still need to be configured separately for their respective annotation types.

### `[annotations.highlight]` - highlight annotation related settings
    - `hover_glow` (bool) to enable/disable hover glow effect on highlight annotation
    - `comment` (bool) to show the comment of the highlight annotation as tooltip on hover (if the annotation has a comment)
    - `comment_marker` (bool) to show a marker (e.g. an icon) on the annotation if it has a comment, to indicate that there's a comment associated with the annotation
    - `glow_width` (int) to set the width of the hover glow effect on highlight annotation
    - `glow_color` (RGBA hex value) to set the color of the hover glow effect on highlight annotation
    - `comment_font_size` (int) to set the font size of the comment tooltip for highlight annotations

### `[annotations.popup]` - popup annotation related settings
    - `hover_glow` (bool) to enable/disable hover glow effect on popup annotation
    - `glow_width` (int) to set the width of the hover glow effect on popup annotation
    - `glow_color` (RGBA hex value) to set the color of the hover glow effect on popup annotation

### `[annotations.rect]` - rectangle annotation related settings
    - `glow_width` (int) to set the width of the hover glow effect on rect annotation
    - `hover_glow` (bool) to enable/disable hover glow effect on rect annotation
    - `comment` (bool) to show the comment of the rect annotation as tooltip on hover (if the annotation has a comment)
    - `comment_marker` (bool) to show a marker (e.g. an icon) on the annotation if it has a comment, to indicate that there's a comment associated with the annotation
    - `glow_color` (RGBA hex value) to set the color of the hover glow effect on rect annotation
    - `comment_font_size` (int) to set the font size of the comment tooltip for highlight annotations

- `enabled` option in `[links]` to enable/disable links
### `[search]` for search related settings
    - `match_color` (RGBA hex value) to set the color of the search hit highlights
    - `index_color` (RGBA hex value) to set the color of the current search hit highlight

### Optimizations

- Pass `struct` const reference of only the respective members instead of the whole `Config` struct.

### New Commands

- `toggle_annot_comment_marker` - Toggle the comment marker on annotations that have comments (e.g. show/hide the icon indicating that there's a comment associated with the annotation)
- `none_mode` - Switch to none interaction mode where no interaction mode is active
- `picker_annot_comment_search` - Open the picker with the list of annotations that have comments, and searching by the comment text. Selecting an entry will jump to the annotation location.

### Breaking Changes

- Rename `LEFT_TO_RIGHT` layout to `horizontal` and `TOP_TO_BOTTOM` layout to `vertical` for better clarity and intuitiveness.
- Renamed `layout_left_to_right` command to `layout_horizontal` and `layout_top_to_bottom` command to `layout_vertical` to reflect the renamed layout modes.
- Command palette is now opened with ":" key by default instead of "Ctrl + Shift + P" to stay close to the Vim way of opening command palette and also because it's more ergonomic to open with a single key press.
- Renamed `[markers]` to `[jump_marker]`
- Removed `[color]` section from config and moved all colors to thier respective sections (e.g. annotation colors are now in `[annotations]` section, search hit colors are now in `[search]` section, etc.) for better organization and maintainability of the config file.


### Bug Fix

- Fix last page number loading
- Fix `[links]` section causing pages to not render in non-link supported files
- Fix page stride when zooming in DJVU files
- Fix zoom locking on the center of the viewport instead of the actual location
- Fix user shortcuts fighting with default shortcuts
- Fix zoom jumping to different page after zooming in/out.
- Fix annotation comment tooltip going off screen
- Fix color setting in the wrong format (ARGB instead of RGBA)
- Fix not able to save after adding comment
- Fix search results not clearing when searching for empty string
- Fix `-p,--page` command line argument not working
- Fix non-exsistent files trying to be opened
- Fix document reload causing unnecessary popup of message box saying it couldn't reload document
- Fix crash when reloading after file changes on disk
- Fix crash and infinite loop when trying to reload after file save
- Fix layout changing at runtime not working properly
- Fix crash due to zero image size in BOOK layout mode

## 0.6.4

### Bug Fix

- Fix memory leaks due to not freeing up the cloned MuPDF `fz_context`
- Fix `search` not working because of recent changes to page text caching
- Fix page getting cut off in layout modes other than BOOK
- Fix search navigation being all jumpy and causing head-ache (literally!)
- Fix SINGLE layout
- Fix LEFT_TO_RIGHT and TOP_TO_BOTTOM layout
- Fix highlighting crash due to context removal
- Fix split focus
- Fix `page_goto` dialog defaulting to page number 1 instead of current page number
- Fix `page_goto` requesting render even when the dialog is closed without input
- Fix multi click text selection

### Features

- Implement batch searching for faster search result fetching as soon as few searches are encountered.
- Regex searching
- Book (two-page) layout `[layout] { mode = "book" }`
- Ability to select text across page boundaries in layout modes with multiple pages visible
- Visual Line Mode
    - Added visual line mode for text selection that can be toggled with `visual_line_mode` command. In this mode, when you select a line of text, the selection will automatically extend to the beginning and end of the line, making it easier to select whole lines of text without having to precisely click at the start or end of the line.
    - Snaps when moving to different pages
    - mouse click in visual line mode to select the line

### New Commands

- Ability to set marks to locations in the document (both local and global level). Local marks are marks that are local to the document, whereas global marks are associated with particular document and when called will switch focus and go to the mark location in that document.
  - `mark_goto` - Ask user for the mark key and go to the location if key is valid
  - `mark_set` - Ask user for key to set the mark (local mark key start with lowercase letter or word and global starts with uppercase)
  - `mark_delete` - Ask user for the mark key to delete and delete if the key is valid
- `search_regex` - Opens searchbar with regex enabled
- `layout_book` - Book layout mode (Two-page layout)
- `mouse_follows_focus` - when enabled, the mouse will automatically move to the center of the focused view when switching focus between splits or portals. This is useful for users who want to keep their hands on the keyboard and avoid having to move mouse manually to the view they just focused.

### New Config Options

- `[portal]`
  - `enabled` (bool) : Enable portal (ctrl + click on link behavior)
  - `respect_parent` (bool): Close portal when the source view is closed
  - `border_width` (int): Width of the border around the view signifying that the view is a portal

### Optimizations

- Don't pass QPointF and QRectF as const references as it's useless.
- Cache `fz_stext_page*` (performance boost)

### Breaking Changes

- Renamed command:
  - `portal_focus` -> `portal`
  - Commands that begun with `toggle_` are renamed to just the action they do without the toggle prefix (e.g. `toggle_command_palette` -> `command_palette`, `toggle_fullscreen` -> `fullscreen`, etc.) because it's more intuitive to have the command name reflect the action it does rather than the implementation (i.e. whether it toggles or shows/hides explicitly)

- `startup_tab` widget is not enabled by default anymore

## 0.6.3

### Features

- Centralised `picker` widget for both `outline` and `search highlights` (and potentially other similar widgets in the future). This allows us to reuse the code for both features and also allows us to easily add new features that require a similar UI in the future.
- Orderless, space-aware searching ability for pickers by default (e.g. "open file", "file open" both will match "file_open" command)
- Add recent files picker (command: `files_recent`) to quickly access recently opened files
- Per page dimension support (instead of using the dimensions of the first page for the entire document, which causes issues with documents that have pages of different sizes)
- New config option
    - `[window]`
        - `initial_size` (array of 2 ints): Initial window size in pixels (width, height)
    - `[rendering]`
        - `antialiasing` (bool): Enable/Disable antialiasing for rendering (default: true)
        - `text_antialiasing` (bool): Enable/Disable antialiasing for text rendering (default: true)
        - `smooth_pixmap_transform` (bool): Enable/Disable smooth pixmap transformation when scaling (default: true)
    - `[behavior]`
        - `preload_pages` (int): Number of pages to preload before and after the current page for smoother navigation (default: 5)
    - `[selection]`
        - `copy_on_select` (bool): Copies selection to clipboard when text is selected (default: false)
- Moved the tab split count to the beginning of the tab instead of it being near the close button of the tab

### Optimizations

- Fix MessageBar's showMessage causing resizeEvent of GraphicsView causing re-rendering of displayed pages
- Make `text_cache` an LRU cache to reduce memory usage on large documents (used for text searching)
- Use page dimension cache (instead of repeatedely querying the document for dimension) for `toPDFSpace` and `toPixelSpace` functions to improve performance when converting coordinates between PDF space and pixel space, especially for documents with many pages or varying page sizes.
- Memory efficient QImage for page rendering and avoiding conversion to QPixmap. (Huge performance boost)

### Breaking Changes

- Remove `[outline]` and `[search_highlight]` sections from the config.
- Rename `[overlays]` to `[picker]` which is a more accurate name for what it does.
- Add `[picker.keys]` section for keybindings related to the picker navigation keys (which applies to all picker widgets like outline, search highlights, recent files, etc.)
- Removed panel settings from `[outline]` and `[search_highlight]` section as overlays are the only variant supported for now.
- Removed `config_auto_reload` from `[behavior]` as it was never supported.
- Removed `icc_color_profile` from `[rendering]` as it was never supported.

#### Renamed commands

- `yank` -> `selection_copy`
- `cancel_selection` -> `selection_cancel`
- `reselect_last_selection` -> `selection_last`
- `first_page` -> `page_first`
- `last_page` -> `page_last`
- `next_page` -> `page_next`
- `prev_page` -> `page_prev`
- `goto_page` -> `page_goto`
- `prev_location` -> `location_prev`
- `next_location` -> `location_next`
- `close_split` -> `split_close`
- `focus_split_left` -> `split_focus_left`
- `focus_split_right` -> `split_focus_right`
- `focus_split_up` -> `split_focus_up`
- `focus_split_down` -> `split_focus_down`
- `close_other_splits` -> `split_close_others`
- `focus_portal` -> `portal_focus`
- `save_session` -> `session_save`
- `save_as_session` -> `session_save_as`
- `load_session` -> `session_load`
- `text_select_mode` -> `selection_mode_text`
- `region_select_mode` -> `selection_mode_region`
- `open_file_tab` -> `file_open_tab`
- `open_file_vsplit` -> `file_open_vsplit`
- `open_file_hsplit` -> `file_open_hsplit`
- `open_file_dwim` -> `file_open_dwim`
- `close_file` -> `file_close`
- `reload` -> `file_reload`
- `encrypt` -> `file_encrypt`
- `save` -> `file_save`
- `save_as` -> `file_save_as`
- `tab1` -> `tab_1`
- `tab2` -> `tab_2`
- `tab3` -> `tab_3`
- `tab4` -> `tab_4`
- `tab5` -> `tab_5`
- `tab6` -> `tab_6`
- `tab7` -> `tab_7`
- `tab8` -> `tab_8`
- `tab9` -> `tab_9`
- `about` -> `show_about`
- `outline` -> `picker_outline`
- `highlight_annot_search` -> `picker_highlight_search`
- `command_palette` -> `toggle_command_palette`
- `tutorial_file` -> `show_tutorial_file`
- `show_startup` -> `show_startup_widget`
- `fullscreen` -> `toggle_fullscreen`
- `auto_resize` -> `fit_auto`
- `text_highlight_current_selection` -> `highlight_selection`

### New commands

- `reopen_last_closed_file` - Reopens the recently closed file in a new tab
- `copy_page_image` - Copy the current page as image to clipboard
- `file_decrypt` - Decrypt the current PDF file
- `annot_popup_mode` - Enter popup annotation creation mode
- `reshow_jump_marker` - Reshow the jump marker at the current location (useful if it was hidden after timeout)

### Optimizations

- Optimize `pageSceneAtPos` function to use a more efficient way of finding the page at a given position, which should improve performance when clicking on links or navigating to specific locations in the document.
- Maintain a vector for `LinkHint` pointers for efficient access and management of link hints, which should improve performance when showing and hiding link hints.
- Optimize `clearDocumentItems` function to efficiently clear all items related to a document when it's closed, which should improve performance and reduce memory usage when closing documents.

### Bug Fixes

- Fix tabs not respecting the `tabs.position` config option
- Fix for tab title not updated when closing split
- Fix for tab title not updated which changing current split focus

## 0.6.2

### Features
- Ability to create portals:
    - A portal is a view of the same document but viewing different page or location in the document. Portals are useful for referencing different parts of the same document side by side, for example when reading a research paper and wanting to view the references at the same time. (Ctrl + click on a link to open the target in a new portal)
- Ability to move tabs with mouse drag and drop within the tab bar and detach to new window if dropped outside
- Ability to split the view into multiple panes to view different pages of the same document (or different) side by side
- Ability to focus split with direction

- New flags for opening files in `vsplit` or `hsplit` directly from the command line
    - `--vsplit` - Open file in a vertically split pane
    - `--hsplit` - Open file in a horizontally split pane
- New config options for splits
    - `[split]`
        - `focus_follows_mouse` (bool): Whether to focus split pane on mouse hover
        - `dim_inactive` (bool): Whether to dim inactive split panes
        - `dim_inactive_opacity` (float, 0-1): Opacity level for dimming inactive panes (default: 0.5)
- New commands for splits
    - `split_horizontal` - Split the current view horizontally
    - `split_vertical` - Split the current view vertically
    - `close_split` - Close the current split pane
    <!-- - `focus_split_left`, `focus_split_right`, `focus_split_up`, `focus_split_down` - Move focus between split panes in the specified direction -->
    - `focus_split_left`, `focus_split_right`, `focus_split_up`, `focus_split_down` - Move focus between split panes in the specified direction (if no pane in that direction, do nothing)
    - `open_file_vsplit` - Open a file in a vertically split pane
    - `open_file_hsplit` - Open a file in a horizontally split pane
- New command for opening file
    - `open_file_dwim` - Open file with "Do What I Mean" behavior: if there's a tab open with no splits, open the file in new tab, if there's a tab open with splits, open the file in current split.

### Bug Fixes

- Fix tab drop opening new window instead of moving tab when dropped within the same window
- Fix window focus changing the dimmed state of split panes
- Fix crashing on splitting with huge documents.
- Remove filepath hash to keep track of already opened files, instead use the file path directly which should fix many other issues related to file opening and session restore
- Fix session loading layout restoration to work with the new splits system
- Fix `open_file_dwim` not working properly in some cases
- Fix callback not being called after file open in some cases (due to async file opening)
- Fix panel and tab not showing file info properly on file single file open
- Hide scrollbars when the entire document is visible in the viewport
- Fix not using config zoom factor
- Fit incorrect zoom on file open
- Fix rendering bug when there's no value for DPR set in the config
- Fix TabWidget logo font
- Fix zoom clamp range

### Breaking Changes
- Removed `ui` table in config and moved all options to their respective sections
    - `[ui.command_palette]` -> `[command_palette]`
    - `[ui.scrollbars]` -> `[scrollbars]`
    - `[ui.statusbar]` -> `[statusbar]`
    - `[ui.tabs]` -> `[tabs]`
    - `[ui.outline]` -> `[outline]`
    - `[ui.search_highlight]` -> `[search_highlight]`
    - `[ui.llm_widget]` -> `[llm_widget]`
    - `[ui.overlays]` -> `[overlays]`

## 0.6.1 [Lektra Update]
#### Renamed project name from `dodo` to `lektra`

### Features
- Add support for other MuPDF compatible file types: EPUB, XPS, CBZ, MOBI, FB2, SVG
- New Commands
    - `tab_move_left`, `tab_move_right` - move tabs in the tabBar left/right
- History navigation improvements
    - Forward/next-location history navigation with `next_location`
    - Preserve link source/target locations so jump markers land correctly
- Add `--about` command line argument to show about dialog

### Bug Fixes
- Disable access to annotation mode when not in PDF file (for other file formats)
- Remove PDF word from the file properties window title
- Fix text selection context menu takes precedence over annotation highlights when both apply
- Fix scrollbar auto-hide timer ignoring configured hide timeout after mouse leave
- Fix recent files page restore to open the stored page without adding history entries

### Breaking Changes

- Remove `close_file` command as it was redundant with `tab_close` (which does the same thing)
- Rename `bar_position` to `location` in `[ui.tabs]`

## 0.6.0

### Features

- Add ability to copy unformatted text (removes hyphenation and joins lines)
- Add delete annotations implementation when in `annot_select_mode`
- Increased rendering performance.
- Add `tutorial_file` command to show a tutorial PDF file
- Added `role.txt` system prompts for the LLM
- Implement LRU (Least Recently Used) cache for reduced memory usage
    - Respect `behavior.cache_pages` config option for this
- Add frame border around the overlay widgets
- Replace `setdpr` command with `set_dpr`. This command now opens a popup input dialog asking for the new Device Pixel Ratio (DPR) value.
- Drag-drop file handling
    - Hold `Shift` while dropping a file onto the main view to open in a new window
- Improved Startup Page
- `goto_page` command now registers current location for history navigation back to the page from where the command was called.

### Config options
- Page foreground and background color config option
    - `[colors]`
        - `page_foreground` (RGBA hex value)
        - `page_background` (RGBA hex value)
- Tab close commands `[keybindings]`
    - `tabs_close_left` - close all tabs to the left of the current tab
    - `tabs_close_right` - close all tabs to the right of the current tab
    - `tabs_close_others` - close all tabs except the current tab
- New options for tabs `[ui.tabs]`
    - Lazy loading `lazy_load` (bool) - loads only when switching to the tab (useful when loading lots of papers)
    - File path in tab `[ui.tabs]`
        - `full_path` - whether to show full path in the tab
- Overlay frame config `[ui.overlays]`
    - `border` - show border around overlay frames
    - `shadow` table
        - `enabled` - toggle shadow on overlay frames
        - `blur_radius` - shadow blur radius in pixels
        - `offset_x` - horizontal shadow offset
        - `offset_y` - vertical shadow offset
        - `opacity` - shadow opacity (0-255)

### Optimizations

- Set optimization flag and CacheMode for QGraphicsView
- Colors are now stored as packed RGBA ints and parsed from hex config values

### Breaking Changes

**NOTE**: You might have to change few things in the config

- Command `first_tab`, `last_tab` renamed to `tab_first`, `tab_last`
- Remove `Fit None` from the fit menu
- Moved `Auto Resize` checkbox to the view menu instead of fit menu

### Bug Fixes

- Fix startup page layout
- Fix orderless completion not working in the command palette
- Rename startup tab to `Startup Page`
- Fix double free in document cleanup
- Fix link hover cursor change
- Fix link navigation when target page is not yet rendered
- Hide the command palette before executing the command
- Fix command pallette showing only set keybound commands
- Fix tab close commands not working as expected
- Fix cursor not changing when hovering over link
- Fix right click context menu on tabs
- Command palette orderless matching consider underscore as literal when entered.
- Cancel page rendering request when starting to scroll
- Placeholder page items are just upscale pixmaps to reduce cpu and memory usage
- Render visible pages instead of just reloading single page when file changes in disk
- Don't cache `fz_stext_page` (very memory intensive)
- LLM Widget
    - Disable send button in the LLM widget when there's no query
    - Message when provider server is not found to be running
- Fix scroll commands not triggering scrollbar visibility (for auto_hide = true)
- Fix document not reloading even when `[behavior.auto_reload]` is set to true
- Hide searchbar when the search term is empty
- Fix loading default config when error is found in config
- Fix highlights search overlay hiding after selecting entry

## 0.5.6

### Bug Fixes

- Fix dodo not compiling without synctex

## 0.5.5

### Features

- Change color of selected annotations
- Allow editing popup annotations (also called text annotations)
- Visual feedback when hovering over popup annotation icons
- Statusbar config option
    - `[ui.statusbar]`
        - `visible` (bool): Show/Hide statusbar
        - `padding` (array of 4 ints): Padding around the statusbar (top, right, bottom, left)
        - `show_session_name` (bool): Show session name in statusbar
        - `file_name_only` (bool): Show only file name instead of full path
        - `show_file_info` (bool): Show file info (size, number of pages) in statusbar
        - `show_page_number` (bool): Show current page number in statusbar
        - `show_mode` (bool): Show current mode (e.g. selection mode) in statusbar
        - `show_progress` (bool): Show reading progress in statusbar
- Percentage progress indicator in statusbar

### Bug Fixes

- Fix rect annotation creation
- Fix popup annotation creation

## 0.5.4

### Features

- **Command palette improvements**
    - Sort commands alphabetically
    - Right-align shortcuts and optionally hide them (`[ui.command_palette].show_shortcuts`)

- **Overlay scrollbars**: Scrollbars now appear as floating overlays that don't shift the layout
    - Auto-hide after configurable timeout (default 1500ms)
    - Scrollbars remain visible while dragging or hovering over them
    - Scrollbars flash on zoom in/out/reset
    - New config options in `[ui.scrollbars]`:
        - `auto_hide` (bool): Auto hide scrollbar when not in use
        - `size` (int): Scrollbar width/height in pixels (default: 12)
        - `hide_timeout` (int): Milliseconds before hiding after inactivity (default: 1500)

- **Detach from terminal** - By default dodo will now detach from the terminal when launched from a terminal. This can be disabled by using the `--foreground` command line argument.

- **Add LLM support \[OPTIONAL\]**
    - LLM (Large Language Model) integration is entirely optional and can be disabled completely from the code by the compile flag `ENABLE_LLM_SUPPORT`.
    - Integrate with local LLM models to provide AI-powered assistance
    - Config options:
        - `[llm]`
            - `enabled` (bool): Enable/Disable LLM assistance
            - `model_path` (string): Path to the local LLM model
            - `temperature` (float): Sampling temperature for response generation
            - `max_tokens` (int): Maximum tokens in the generated response

- **Region Selection context menu**
    - Right click context menu when in region selection mode
        - Copy text in region to clipboard
        - Copy region as image to clipboard
        - Save region as image to file
        - Open region as image externally


### Bug Fixes

- Fix scrollbar disappearing while actively dragging
- Fix scrollbar hiding when quickly changing scroll direction
- Fix scrollbar handle size not updating after zoom
- Add visual feedback to current keypress in the link hinting mode
- Hide non-matching link hints as keys are entered and dim the typed digits
- Fix config color parsing to treat 8-digit hex values as RGBA
- Add orderless, space-aware command palette search

## 0.5.3


### Features


- **Cross-window tab drag and drop**: Tabs can now be dragged between windows or detached to create new windows.
- Change cursor when selecting or highlighting text
- Searchable text highlights
- Outline widget and Search Highlight widget types - "dialog", "side_panel", "overlay" (configurable in settings)
    - UI
        `[ui.outline]` - `type=<value>` where value = `dialog`, `side_panel`, `overlay`
        `[ui.search_highlight]` - `type=<value>` where value = `dialog`, `side_panel`, `overlay`
- Detection of non-link URL pdf object in the PDF and making them clickable links (optional, configurable in settings)
    - [ui.links]
        - `detect_urls` - Enable/Disable non-link URL detection and linkification
        - `url_regex` - Custom regex for URL detection (default is a standard URL regex)

- New options in `[behavior]`:
    - `cache_pages` (int): Maximum number of pages to keep cached per document. Default is `20`.
    - `always_open_in_new_window` (bool): If true, files will always open in a new window instead of a new tab. Default is `false`.



### Bug Fixes
- Remove stray return causing panel update issues
- Fix prompting for password protected documents
- Fix session loading not opening files properly - files now load correctly with their saved state (page, zoom, fit mode, invert color)
- Fix session page restoration using wrong page index (1-indexed vs 0-indexed)
- Fix early return in `openFileFinished` callback that prevented the callback from being executed for documents with outlines
- Fix context menu not showing at the right position
- Fix fit width not working properly in some cases
- Fix startup Page widget not respecting fit mode from config
- Fix scrolling to specific position and center it in the viewport
- Fix zoom changing the viewport position unexpectedly
- Show highlight search overlay properly
- Fix double memory free crash on exit when there's more than one tab
- Show information if no outline is present in the document instead of empty outline panel
- Fix crash when rendering link/annotation items from background threads

## 0.5.2

- Add runtime layout switching
- Link hinting mode for keyboard link navigation
- Updated organized configuration settings (check config.toml)
- Fix first page and last page offset in LEFT_TO_RIGHT and TOP_TO_BOTTOM layouts
- Make scrollbar handle transparent to allow search hits marker visibility
- Show placeholder page when pages are loading
- Use queue for managing threads while rendering instead of spawning threads for each render request
- Make asynchronous to avoid UI blocking on large documents
- Add min and max zoom levels
- Add Link Keyboard hinting mode
- Show loading spinner when loading huge documents
- Add rotation support
- Fix annotation deleting
- Fix panel name modified status not updating properly
- Fix annotation select at point and rectangle selection
- Fix highlight annotation rendering issues
- Fix highlight annotation coordinate mismatch

### Bug Fixes

- Fix invert color not reloading already visible pages
- Fix LEFT_TO_RIGHT layout not showing vertical scrollbar
- Fix link clicking
- Fix context menu positioning


## 0.5.1

### Features
- Search hits marker on the scrollbar
- Config option to enable/disable search hits marker on scrollbar
    - UI
    `search_hits_on_scrollbar`
- Search is now smart case by default (i.e. case insensitive unless uppercase letters are used in the search query)
- Search Bar improvements
    - Show only icons and not text in the search bar buttons
- Close file in tab
- Preload next and previous pages for faster navigation (configurable in settings)
    - Rendering
        `preload_adjacent_pages`
        `num_preloaded_pages`
- Layouts
    - Single Page
    - Left to Right
    - Top to Bottom
- Config option to set default layout on file open
    - UI
        `layout` - values: `single`, `left-to-right`, `top-to-bottom`

### Bug Fixes

- Click on links not working properly in some cases
- Fix navigation from menu
- Fix synctex to work with new rendering system
- Significantly improved rendering performance by optimizing memory copying from MuPDF to Qt
- Fix go back history not working properly
- Single page layout issues
    - Link item deletion
    - Page navigation
- History navigation issues
## 0.5.0

### Features
- Switch from single view rendering to tile based rendering like sane PDF viewers
- Huge rewrite of the rendering system
    - Asynchronous rendering
    - Improved performance


### Bug Fixes
- Fix annotation rendering issues
- Fix annotation position issues on zoom and pan

## 0.4.3
- Zoom render debounce to improve performance
- Make fit window, fit width, fit height work properly with the new debounce logic

## 0.4.2
- Add popup annotation menu item
- Edit last pages widget disable when no entries exist
- Don't render highlight annotations as it's done by MuPDF
- Merge highlight annotations on overlapping areas


## 0.4.1
- Remove padding around the document view
- Fix installation script
- Fix build issues
- Fix .desktop file installation
- Fix skinny statusbar
- Fix panel file name display (absolute vs relative path)
- Fix session file overwriting without prompt
- Fix session path to file
- Add session button to panel
- Fix session button label
- Fix synctex not working after reloading file
- Update session files if in session on application exit
- Fix loading session opening second window on launch

## 0.4.0

- Implement document reloading on external changes
- Remove command bar
- Implement clean search bar UI

## 0.3.1

### Features

- Undo/Redo support for annotations
    - Rect annotations
    - Text highlight annotations
    - Delete annotations
- Multi-click text selection (double, triple, quadruple click for word, line, paragraph)
- Page rotation (clockwise and counter-clockwise)
- Editable page number in panel (click to edit and navigate)
- Tab navigation keybindings (tab_next, tab_prev, tab1-tab9)
- Config options:
    - Behavior
        - `undo_limit` - Maximum number of undo operations to keep

### Bug Fixes

- Jump marker fading animation fix
- Fix unsaved changes being asked multiple times
- Centralized unsaved handling

## 0.3.0

### Features

- Fix copy text popup when no text is selected
- Wonky selection handling
- Fix scaling problem in single monitor systems
- Multi-monitor user defined scaling support
- Region select mode
    - Copy text from selected region to clipboard
    - Copy selected region as image to clipboard
    - Save selected region as jpeg/png etc. image
    - Open selected region externally
- Image right click context menu
    - Copy image to clipboard
    - Save image as jpeg/png/psd
    - Open image externally
- Display Pixel Ratio user config option (fix blurry text on high DPI screens)
- Outline widget as a sidebar option
- Outline widget page numbers displayed properly
- Outline hierarchy search support
- Config options:
    - UI
        - `outline_as_side_panel`
        - `outline_panel_width`
        - `link_hint_size`
    - Rendering
        - `dpr`
- Popup annotations improved

### Bug Fixes

- Fix buggy selection behavior after clicking
- Fix blurry text on high DPI screens
- Fix popup annotation not deleting properly

## 0.2.4

- Fix drag and drop not opening files
- Popup Annotation support
- Page navigation menu disable unintended buttons

## 0.2.3

- Fix startup tab on launch with files
- Vim-like search in commandbar
- Goto page from commandbar
- Fix goto page
- Fix link clicking mouse release event
- Fix recent files addition
- Fix panel and other actions updating on tab switching

## 0.2.2

- Tabs support
- Sessions support

## 0.2.1

- Bug fixes

## 0.2.0

- Page visit history
- Link visit
    - http/https links
    - Page links
    - XYZ links
- Browse links implemented (URL)
- Basic rectangle annotations
- Search
- Table of Contents
- File properties
- Keyboard link navigation


## 0.1.0

- Render
- Navigation
- Panel
