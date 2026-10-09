# TODO

## This is a list of features and improvements that I want to implement in the future. The list is not exhaustive and is subject to change based on user feedback and my own priorities.

## HIGH PRIORITY

1. return result; inside fz_try (Render.cpp ~796 and ~803). The "Page not cached" and "Missing display list" branches return from inside the try block. MuPDF's fz_try can't be exited with return: the exception stack is left unbalanced, and fz_drop_context(ctx) in fz_always is skipped. Each hit therefore leaks a cloned context. It's most likely to happen under fast scrolling, when the LRU cache evicts a page between ensurePageCached and the render. Set a flag and fz_throw, or leave the block normally, so fz_always runs.

Biggest efficiency wins

2. Cancel renders that are already running (no fz_cookie anywhere).
- fz_run_display_list(..., nullptr) can't be interrupted. The cancel token is only checked before the work starts.
- A page scrolled away mid-render, or a render made stale by a zoom step, still runs to completion on the pool. At deep zoom, that's up to 16M pixels.
- Fix: make the token a small struct holding an fz_cookie next to the atomic flag. Set cookie.abort = 1 when cancelling, and pass &cookie to fz_run_display_list. MuPDF polls it while drawing.
- This directly helps fast scroll and wheel zoom, because stale work stops sooner and frees the in-flight slots (maxInFlight is 2–4).

3. Remove the full-page copy (Render.cpp ~974). QImage(samples,…).copy() copies the whole bitmap, which is about 48 MB at the 16M-pixel cap.
- Allocate the buffer yourself (new unsigned char[stride*h]).
- Wrap it with fz_new_pixmap_with_bbox_and_data.
- Hand it to QImage with a delete[] cleanup function.
- That's zero-copy and needs no MuPDF context afterwards.

4. Reuse the page item instead of recreating it (renderPageFromImage, Rendering.cpp ~1406). Every render result removes and deletes the old GraphicsImageItem, then creates and adds a new one. That causes scene index churn, a flicker risk, and an allocation per render. GraphicsImageItem already has setImage and setPartialImage, so update the existing item in place and only create one when none exists.

5. Don't rebuild links, annotations and search hits when nothing changed. After every result, renderPageFromImage clears them and the callback recreates them item by item. A partial-region refresh at deep zoom, or a same-key re-render, doesn't move any of them. Skip the rebuild when the zoom, rotation and layout match what the existing items were built for.

Medium

6. Pixel format.
- The render path produces Format_RGB888. When Qt paints it with a scale transform, the raster engine converts RGB888 on the fly.
- Format_ARGB32_Premultiplied (or RGB32) is Qt's fast path. Rendering into a BGRA MuPDF pixmap with alpha=255 makes it exact and skips the conversion.
- It costs 33% more memory per page.
- fz_tint_pixmap assumes RGB channel order, so the fg/bg bytes would need swapping.
- Do this one only if a profile shows paint cost during scroll.

7. Fuse the post-processing passes. Tint, invert and high-contrast each do a full pass over the pixmap. Invert and high-contrast are both per-byte lookup tables (the high-contrast one is already a LUT), so they can be combined into one LUT and one pass.

8. Copies under the cache lock. Each render copies the page's links and annotations vectors while holding m_page_cache_mutex, even when the document has none. Keep them in a shared_ptr<const …> in the cache entry and copy only the pointer.

Minor

- renderPagesImpl copies std::set<int> several times on every scroll tick (66 ms). A small sorted vector would avoid the node allocations.
- PageRenderResult and the render callback move large vectors around by value. They're small now, so only worth it after items 2–5.
- The new QFutureWatcher per render is fine. It would only matter if you move to a custom queue.


- [ ] Fix scroll wheel zoom being very slow
- [ ] Increase performance
- [ ] Picker style choose, `minibuffer` or `floating`

## MEDIUM PRIORITY

- [ ] Scroll tab text when hovering on the tab
- [ ] Dynamic Modal Input Engine
    + View:bind_key handles static keymaps, but full modal keybinding state transitions are missing.
    + Missing APIs: `lektra.input.create_mode(mode_name, keymap_table)`
    + `lektra.input.enter_mode(mode_name)`
    + `lektra.input.exit_mode()`
    + Use Case: Entering custom sub-modes (e.g., an "Annotation Mode" where h highlights, u underlines, and d deletes the target under cursor, or a Leader-key chord system like <Leader>f).
- [ ] Annotation save as temp file and auto-save on exit
- [ ] Macros
- [ ] Multi-Document / Workspace Search
- [ ] Telescope / FZF-style Fuzzy Text Search Across Document
- [ ] Zotero / BibTeX Integration & Reference Inspection
- [ ] Grid / Multi-Page Thumbnail Overview Sheet
- LLM View
    - [x] Chat History
    - [x] Image attachment
    - [ ] File attachment
    - [x] Lua code block syntax highlighting
    - [x] Lua API code block execution
    - [x] latex math rendering
    - [ ] `llm_view.max_history_messages` — sliding window: only send the last N message pairs to the model, drop oldest beyond that
    - [ ] Summarization — when history exceeds a threshold, ask the model to summarize the conversation and replace old messages with the summary
    - [ ] `llm_view.extra_body` passthrough for Ollama options (e.g. `{ options = { num_ctx = 8192 } }`) to increase local model context window beyond the 2048 default

- [ ] Make tab sizes fixed while closing tabs rapidly (like Chrome) instead of shrinking them to fit
- [ ] Pin Tabs
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
- [ ] Add support for embedded files in PDFs
- [ ] Underline Annotation
- [ ] EPUB/reflowable documents render as discrete book-like pages instead of a genuinely continuous flow (unlike mupdf). Root cause: `fz_layout_document(w, h, em)` chops each chapter's continuous flowed content into pages by dividing total flow height by `h` (`epub-doc.c`'s `ceilf(layout.b / page_h)`) — Lektra always passes a physical-page-sized `h` (`Model::layoutHeightPts()`), so every chapter gets fragmented into many short pages with hard breaks; `layout.spacing = 0` only hides the gap between items, it doesn't remove the underlying pagination. Fix: pass a much larger `h` for reflowable documents so each chapter becomes one continuous page. Tradeoff to resolve first: a chapter then renders as one potentially very tall bitmap — the existing viewport-clipped rendering (0.7.5) only applies above 2.5x zoom, so it may need extending to cover this case (or the height capped at some large-but-bounded value as a middle ground) to avoid rendering a whole long chapter in one shot at normal zoom.

## LUA PLUGIN IDEAS

- [x] OCR with tesseract
- [x] Text selection word count
- [x] Read Aloud (text-to-speech)
- [ ] Equation OCR to LaTeX
- [ ] Table exporter to tex/CSV/Excel/Numpy
- [ ] Semantic search
- [ ] Finding citation from folder
- [ ] Explain selection
- [ ] Search inside math equations
