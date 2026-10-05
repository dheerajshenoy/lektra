-- Examples of small plugins for init.lua.
--
--   tesseract         OCR the current page (lektra.async, lektra.job, lektra.paths, lektra.statusbar)
--   word_count        count the words of the selected text
--   words (segment)   words on the current page, shown in the statusbar
--   annotation_notes  copy the highlights and comments of the document to the clipboard
--   highlight_page    highlight a band at the top of the current page
--   sync_picked       pick views of this tab and link their zoom and scroll

-- Runs tesseract (OCR) on the current page and opens the text in a new tab.
--
-- Inside lektra.async the slow steps (the dialog, the job) are written one
-- after the other: lektra.job.await waits for the command without freezing the
-- window, and the code goes on from there.
lektra.cmd.register("tesseract", function()
    lektra.async(function()
        local view = lektra.view.current()
        if not view then
            lektra.ui.message("No document is open")
            return
        end

        local out = lektra.ui.file_dialog("save", {
            title = "Save OCR output",
            filters = { "Text (*.txt)", "All files (*)" },
            default_suffix = "txt",
        })
        if not out then
            return
        end

        -- tesseract adds ".txt" to the output name itself
        local base = out:gsub("%.txt$", "")

        -- the page as an image, at a resolution that suits OCR, in the cache folder
        local png = ("%s/ocr-%d.png"):format(lektra.paths.cache(), os.time())
        local files, err = view:export_pages(png, view:pageno(), { dpi = 300, overwrite = true })
        if not files then
            lektra.ui.message("Failed to export page as image: " .. tostring(err))
            return
        end

        -- a segment of the statusbar shows that it is working
        lektra.statusbar.set("ocr", "OCR…", { tooltip = "Tesseract is running" })

        -- the order matters: image, output name, then the options
        local r = lektra.job.await({ "tesseract", png, base, "-l", "eng" }, { timeout = 300 })

        os.remove(png)
        lektra.statusbar.set("ocr", nil)

        if r.ok then
            lektra.ui.message("Tesseract OCR completed")
            lektra.cmd.execute("file_open_tab", { base .. ".txt" })
        else
            lektra.ui.message("Tesseract failed: " .. (r.error or r.stderr):sub(1, 200))
        end
    end)
end, "OCR the current page with tesseract")

-- Function to count the number of words in the selected text and display it in a message box.
lektra.cmd.register("word_count", function ()
    local view = lektra.view.current()
    if not view then
        return
    end

    if not view:has_selection() then
        lektra.ui.message("No text selected")
        return
    end

    local word_count = 0

    local text = view:selection_text()

    for _ in text:gmatch("%S+") do
        word_count = word_count + 1
    end

    lektra.ui.message("Word count: " .. word_count)
end, "Count the words of the selected text")

-- A piece of the statusbar: the words on the current page. The function is
-- called again when the page, zoom or file changes. It goes to the right end
-- of the bar, or where "words" is put in statusbar.layout.
lektra.statusbar.register("words", function(view)
    if not view then
        return nil -- nothing to show
    end
    local count = 0
    -- (the text is nil while the document is still loading)
    for _ in (view:page_text() or ""):gmatch("%S+") do
        count = count + 1
    end
    return { text = count .. " words", tooltip = "Words on this page" }
end)

-- Copies every highlight (with its text and comment) of a PDF to the clipboard.
lektra.cmd.register("annotation_notes", function()
    local view = lektra.view.current()
    if not view then
        return
    end

    local annotations, err = view:annotations() -- the whole document
    if not annotations then
        lektra.ui.message("No annotations: " .. tostring(err))
        return
    end

    local lines = {}
    for _, a in ipairs(annotations) do
        if a.type == "highlight" and a.text ~= "" then
            local line = ("p. %d: %s"):format(a.page, a.text)
            if a.comment ~= "" then
                line = line .. "  — " .. a.comment
            end
            lines[#lines + 1] = line
        end
    end

    lektra.clipboard.set(table.concat(lines, "\n"))
    lektra.ui.message(#lines .. " highlights copied")
end, "Copy the highlights of the document to the clipboard")

-- Highlights a band at the top of the current page, with a comment.
-- Rectangles are in page points, from the top left of the page.
lektra.cmd.register("highlight_page", function()
    local view = lektra.view.current()
    if not view then
        return
    end

    local width = view:page_size()
    local id, err = view:add_highlight(view:pageno(), { x = 36, y = 36, w = width - 72, h = 14 }, {
        color = "#ffd54f",
        comment = "Added by highlight_page",
    })
    if not id then
        lektra.ui.message("Could not highlight: " .. tostring(err))
        return
    end
    -- the highlight can be undone with the undo command
    lektra.ui.message("Highlighted (annotation " .. id .. ")")
end, "Highlight the top of the current page")

-- Lets you pick views of this tab (numbers are drawn on them) and links their
-- zoom and scroll. Picking fewer than two views does nothing.
lektra.cmd.register("sync_picked", function()
    local ids = lektra.view.pick_views()
    if not ids then
        return -- cancelled
    end
    if #ids < 2 then
        lektra.ui.message("Pick at least two views")
        return
    end
    lektra.view.sync(ids)
    lektra.ui.message("Synced " .. #ids .. " views")
end, "Pick views to sync")
