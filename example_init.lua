-- Example init.lua for Lektra.
--
-- This file is NOT loaded automatically from here — it's a reference/demo,
-- not the live config. Lektra loads init.lua from the config directory:
--   Linux/macOS: ~/.config/lektra/init.lua
--   Windows:     %APPDATA%\lektra\init.lua
-- Copy whatever you want from this file into that location.
--
-- Full API reference: LUA-WIKI.md at the repo root.

-- ---------------------------------------------------------------------
-- Global events
-- ---------------------------------------------------------------------
-- lektra.event.register/.once accept either a plain string event name
-- ("OnFileOpen") or the lektra.event.EventType enum value — the string
-- form is shorter and is what editors type-check reliably.
--
-- init.lua runs once at startup, before any document is open, so this
-- is the only way to hook "the next file that opens, in any view" — a
-- view-scoped view:register() can't be used yet because no view exists.

lektra.event.once("OnAppReady", function()
    lektra.ui.message("Lektra is ready", 2)
end)

lektra.event.register("OnFileOpen", function(view)
    if view then
        lektra.ui.message("Opened: " .. view:file_path(), 2)
    end
end)

-- ---------------------------------------------------------------------
-- Per-view events
-- ---------------------------------------------------------------------
-- Once you have a specific view (e.g. from lektra.view.current(), or
-- the `view` argument an event callback already gives you), you can
-- attach listeners scoped to just that view. Per-view events also use
-- string names.

lektra.event.register("OnFileOpen", function(view)
    if not view then return end

    view:register("OnPageChanged", function(v)
        lektra.ui.message(
            string.format("Page %d / %d", v:pageno(), v:page_count()), 1)
    end)
end)

-- ---------------------------------------------------------------------
-- Custom commands
-- ---------------------------------------------------------------------
-- Registered commands show up in the command palette (Ctrl+Shift+P by
-- default) and can be bound to a key like any built-in command.

lektra.cmd.register("word_count", function()
    local v = lektra.view.current()
    if not v then return end

    local text = v:extract_text(false)
    local _, words = text:gsub("%S+", "")
    lektra.ui.message("Words on this page: " .. words, 3)
end, "Count words on the current page")

lektra.keymap.set("word_count", { "Ctrl+Alt+W" })

-- ---------------------------------------------------------------------
-- Timers
-- ---------------------------------------------------------------------
-- Timers are parented to the main window and auto-cleaned on shutdown,
-- so you don't have to call :destroy() yourself in simple cases.

lektra.event.register("OnFileOpen", function(view)
    if not view then return end

    local reminder = lektra.timer.new(30000, function()
        lektra.ui.message("Still reading? " .. view:file_path(), 2)
    end, true) -- single-shot
    reminder:start()
end)

-- ---------------------------------------------------------------------
-- Context menu customisation
-- ---------------------------------------------------------------------
-- Add an entry to the text-selection context menu that copies the
-- selection in upper case.

lektra.event.register("OnFileOpen", function(view)
    if not view then return end

    view:register_context_menu("TextSelection", function(v, menu)
        menu:add_item("Copy Selection (Upper Case)", function()
            local text = v:selection_text(false)
            lektra.utils.print(text:upper())
        end)
    end)
end)
