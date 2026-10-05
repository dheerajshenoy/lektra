-- Runs tesseract (OCR) on the current page and opens the text in a new tab.
lektra.cmd.register("tesseract", function()
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

    -- the page as an image, at a resolution that suits OCR
    local tmp = os.tmpname()
    local png = tmp .. ".png"
    local files, err = view:export_pages(png, view:pageno(), { dpi = 300, overwrite = true })
    if not files then
        os.remove(tmp)
        lektra.ui.message("Failed to export page as image: " .. tostring(err))
        return
    end

    -- runs in the background; the order matters: image, output name, then the options
    lektra.ui.message("Running OCR...")
    lektra.job.async({ "tesseract", png, base, "-l", "eng" }, { timeout = 300 }, function(r)
        os.remove(png)
        os.remove(tmp)

        if r.ok then
            lektra.ui.message("Tesseract OCR completed")
            lektra.cmd.execute("file_open_tab", { base .. ".txt" })
        else
            lektra.ui.message("Tesseract failed: " .. (r.error or r.stderr):sub(1, 200))
        end
    end)
end)

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
end)

