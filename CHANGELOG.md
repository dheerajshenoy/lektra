# dodo

## 0.6.3

### Features

- Centralised `picker` widget for both `outline` and `search highlights` (and potentially other similar widgets in the future). This allows us to reuse the code for both features and also allows us to easily add new features that require a similar UI in the future.
- Add recent files picker (command: `files_recent`) to quickly access recently opened files

### Breaking Changes

- Remove `[outline]` and `[search_highlight]` sections from the config
- Rename `[overlays]` to `[picker]` which is a more accurate name for what it does
- Add `[picker.keys]` section for keybindings related to the picker navigation keys (which applies to all picker widgets like outline, search highlights, recent files, etc.)

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

### New command

- `file_decrypt` - Decrypt the current PDF file
- `annot_popup_mode` - Enter popup annotation creation mode
- `reshow_jump_marker` - Reshow the jump marker at the current location (useful if it was hidden after timeout)

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
