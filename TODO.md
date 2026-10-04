# TODO

## This is a list of features and improvements that I want to implement in the future. The list is not exhaustive and is subject to change based on user feedback and my own priorities.

## HIGH PRIORITY

- [ ] statusbar layout customization
- [ ] Annotation save as temp file and auto-save on exit

## MEDIUM PRIORITY

- LLM View
    - [x] Chat History
    - [x] Image attachment
    - [ ] File attachment
    - [x] Lua code block syntax highlighting
    - [x] Lua API code block execution
    - [ ] latex rendering

- [ ] Make tab sizes fixed while closing tabs rapidly (like Chrome) instead of shrinking them to fit
- [ ] Pin Tabs
- [ ] Configurable line scroll distance
- [ ] Laser pointer for presentation view
- [ ] Duplicating a tab keeps the page, zoom and scroll position of the original tab.

## LOW PRIORITY

- [ ] Your reading position is saved as a chapter bookmark, so it survives font-size changes. (For reflowable documents, the bookmark is anchored to the chapter and a y-fraction of the chapter's height, so it survives font-size changes. For fixed-layout documents, the bookmark is anchored to the page number.)
- [ ] Click away to deselect an annotation
- [ ] Clickable Plain text DOIs in PDF text
- [ ] Smoother find-as-you-type
- [ ] Tab hover preview
- [ ] Use `djvudec` for DjVu rendering (https://github.com/kjk/djvudec)
- [ ] Citation preview for plain-text references
- [ ] Bookmarks/history/sessions store raw `PageLocation{pageno,x,y}` (`include/PageLocation.hpp`), which an EPUB/reflowable-document relayout invalidates. Short-term (already shipped alongside reflow): clamp to new page count on load (approximately right page, not exact position). Real fix — anchoring EPUB bookmarks to `(chapter, uri-fragment, y-fraction)` like the outline now does (see `Model::resolveOutlineNode`) — is a `PageLocation`/`BookmarkManager` schema change and belongs in its own follow-up.
- [ ] Link hint lua api and then callback to lua
- [ ] Add luajit support
- [ ] Add support for embedded files in PDFs
- [ ] Underline Annotation
- [ ] EPUB/reflowable documents render as discrete book-like pages instead of a genuinely continuous flow (unlike mupdf). Root cause: `fz_layout_document(w, h, em)` chops each chapter's continuous flowed content into pages by dividing total flow height by `h` (`epub-doc.c`'s `ceilf(layout.b / page_h)`) — Lektra always passes a physical-page-sized `h` (`Model::layoutHeightPts()`), so every chapter gets fragmented into many short pages with hard breaks; `layout.spacing = 0` only hides the gap between items, it doesn't remove the underlying pagination. Fix: pass a much larger `h` for reflowable documents so each chapter becomes one continuous page. Tradeoff to resolve first: a chapter then renders as one potentially very tall bitmap — the existing viewport-clipped rendering (0.7.5) only applies above 2.5x zoom, so it may need extending to cover this case (or the height capped at some large-but-bounded value as a middle ground) to avoid rendering a whole long chapter in one shot at normal zoom.


## LUA PLUGIN IDEAS

- [ ] Text selection word count
- [ ] Search region with Google Lens
- [ ] Read Aloud (text-to-speech)
- [ ] Equation OCR to LaTeX
- [ ] Table exporter to tex/CSV/Excel/Numpy
- [ ] Semantic search
- [ ] Finding citation from folder
- [ ] Explain selection
- [ ] Search inside math equations
