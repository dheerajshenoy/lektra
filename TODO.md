# TODO

## This is a list of features and improvements that I want to implement in the future. The list is not exhaustive and is subject to change based on user feedback and my own priorities.

## HIGH PRIORITY

- [ ] Per-filetype config support
- [ ] Add `run_last_command()` command that runs the last command with arguments or whatever was executed interactively.
- [ ] EPUB/reflowable documents render as discrete book-like pages instead of a genuinely continuous flow (unlike mupdf). Root cause: `fz_layout_document(w, h, em)` chops each chapter's continuous flowed content into pages by dividing total flow height by `h` (`epub-doc.c`'s `ceilf(layout.b / page_h)`) — Lektra always passes a physical-page-sized `h` (`Model::layoutHeightPts()`), so every chapter gets fragmented into many short pages with hard breaks; `layout.spacing = 0` only hides the gap between items, it doesn't remove the underlying pagination. Fix: pass a much larger `h` for reflowable documents so each chapter becomes one continuous page. Tradeoff to resolve first: a chapter then renders as one potentially very tall bitmap — the existing viewport-clipped rendering (0.7.5) only applies above 2.5x zoom, so it may need extending to cover this case (or the height capped at some large-but-bounded value as a middle ground) to avoid rendering a whole long chapter in one shot at normal zoom.

## MEDIUM PRIORITY

- [ ] Bookmarks/history/sessions store raw `PageLocation{pageno,x,y}` (`include/PageLocation.hpp`), which an EPUB/reflowable-document relayout invalidates. Short-term (already shipped alongside reflow): clamp to new page count on load (approximately right page, not exact position). Real fix — anchoring EPUB bookmarks to `(chapter, uri-fragment, y-fraction)` like the outline now does (see `Model::resolveOutlineNode`) — is a `PageLocation`/`BookmarkManager` schema change and belongs in its own follow-up.
- [ ] Decorate form fields
- [ ] Trim margins
- [ ] Add support for directory local config files
- [ ] Allow for command arguments
- [ ] Don't add connection to annotation when in non-annotatable mode
- [ ] Link hint lua api and then callback to lua
- [ ] Add luajit support
- [ ] Add support for embedded files in PDFs
- [ ] Underline Annotation

## LUA PLUGIN IDEAS

- [ ] Equation OCR to LaTeX
- [ ] Table exporter to tex/CSV/Excel/Numpy
- [ ] Semantic search
- [ ] Finding citation from folder
- [ ] Explain selection
- [ ] Search inside math equations
