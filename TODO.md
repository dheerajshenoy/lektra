# TODO

## This is a list of features and improvements that I want to implement in the future. The list is not exhaustive and is subject to change based on user feedback and my own priorities.

## HIGH PRIORITY

- [ ] Caret Mode Press F7 (Select Text With Keyboard, CmdSelectTextViaKeyboard) to put a text cursor on the page, like caret browsing in a web browser. Move it with the arrow keys and hold Shift to select. Ctrl + arrows move by word and Home / End / PageUp / PageDown also work. v switches to Vim-style visual mode. Ctrl + C or y copies, and Esc or F7 exits (#4684, #4116).
- [ ] Per-filetype config support

## MEDIUM PRIORITY

- [ ] Trim marings - hides the empty margins around page content, as if they weren’t there. Margins are detected as pages are shown.
- [ ] Your reading position is saved as a chapter bookmark, so it survives font-size changes. (For reflowable documents, the bookmark is anchored to the chapter and a y-fraction of the chapter's height, so it survives font-size changes. For fixed-layout documents, the bookmark is anchored to the page number.)
- [ ] Uniform Page Width (shows pages of different sizes at the same width at percentage zoom levels, using page 1 as the reference)
- [ ] Chrome-style tabs (Ctrl+Tab to switch, Ctrl+Shift+Tab to switch backwards)
- [ ] Configurable line scroll distance
- [ ] Keep view position when turning pages (command)
- [ ] Ebook font and line spacing settings
- [ ] Click away to deselect an annotation
- [ ] Link descriptions on hover
- [ ] Laser pointer for presentation view
- [ ] Tab hover preview
- [ ] Keyboard shortcuts cheat sheet
- [ ] Annotation save as temp file and auto-save on exit
- [ ] Table of contents for comic books from ComicInfo.xml
- [ ] Use `djvudec` for DjVu rendering (https://github.com/kjk/djvudec)
- [ ] Clickable Plain text DOIs in PDF text
- [ ] Show images stored in separate files
- [ ] Citation preview for plain-text references
- [ ] Smoother find-as-you-type
- [ ] Duplicating a tab keeps the page, zoom and scroll position of the original tab.
- [ ] Password on the command line
- [ ] Remove Deleted Files From History command - removes files that no longer exist on disk from the list of recently opened files.
- [ ] Citation and reference hover preview
- [ ] Drag images out of documents
- [ ] Open File Without History (command)
- [x] Digital signature info in Document Properties
- [x] Configurable tab close button (on current tab, on all tabs, or none)
- [x] Page number in tabs
- [x] Links with file:// URIs now open the file as a local document instead of sending it to the browser
- [x] Zoom to Selection
- [x] Files that fail to open get their own tab (title in red)

## LOW PRIORITY

- [ ] Add support for JPEG xl (.jxl) images
- [ ] Add support for JPEG XR (.jxr, .hdp, .wdp, .jfif and .heif) images
- [ ] Add no invert image for DjVu documents
- [ ] Open PDFs inside .p7m files
- [ ] Add `run_last_command()` command that runs the last command with arguments or whatever was executed interactively.
- [ ] Comic book archive support with libarchive instead of MuPDF
- [ ] Bookmarks/history/sessions store raw `PageLocation{pageno,x,y}` (`include/PageLocation.hpp`), which an EPUB/reflowable-document relayout invalidates. Short-term (already shipped alongside reflow): clamp to new page count on load (approximately right page, not exact position). Real fix — anchoring EPUB bookmarks to `(chapter, uri-fragment, y-fraction)` like the outline now does (see `Model::resolveOutlineNode`) — is a `PageLocation`/`BookmarkManager` schema change and belongs in its own follow-up.
- [ ] Decorate form fields
- [ ] Add support for directory local config files
- [ ] Allow for command arguments
- [ ] Don't add connection to annotation when in non-annotatable mode
- [ ] Link hint lua api and then callback to lua
- [ ] Add luajit support
- [ ] Add support for embedded files in PDFs
- [ ] Underline Annotation
- [ ] EPUB/reflowable documents render as discrete book-like pages instead of a genuinely continuous flow (unlike mupdf). Root cause: `fz_layout_document(w, h, em)` chops each chapter's continuous flowed content into pages by dividing total flow height by `h` (`epub-doc.c`'s `ceilf(layout.b / page_h)`) — Lektra always passes a physical-page-sized `h` (`Model::layoutHeightPts()`), so every chapter gets fragmented into many short pages with hard breaks; `layout.spacing = 0` only hides the gap between items, it doesn't remove the underlying pagination. Fix: pass a much larger `h` for reflowable documents so each chapter becomes one continuous page. Tradeoff to resolve first: a chapter then renders as one potentially very tall bitmap — the existing viewport-clipped rendering (0.7.5) only applies above 2.5x zoom, so it may need extending to cover this case (or the height capped at some large-but-bounded value as a middle ground) to avoid rendering a whole long chapter in one shot at normal zoom.


## LUA PLUGIN IDEAS

- [ ] Search region with Google Lens
- [ ] Read Aloud (text-to-speech)
- [ ] Equation OCR to LaTeX
- [ ] Table exporter to tex/CSV/Excel/Numpy
- [ ] Semantic search
- [ ] Finding citation from folder
- [ ] Explain selection
- [ ] Search inside math equations
