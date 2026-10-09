-- Drives the render scenarios for scripts/bench/run_render_bench.py.
--
--   LEKTRA_RENDER_TRACE=1 lektra --foreground -c scripts/bench/render_bench.lua FILE.pdf
--
-- Scenarios (LEKTRA_BENCH_SCENARIOS, default "A,B,C,D"):
--   A  fast scroll        200 steps of about one screen, 16 ms apart
--   B  zoom storm         30 zoom steps of +10%, 60 ms apart, then settle
--   C  deep zoom pan      zoom to 8x, then 100 pan steps, 30 ms apart
--   D  page-down repeat   40 steps of one screen, 80 ms apart (a held key),
--                         then stop: how fast is the page you land on drawn
--
-- It prints "BENCH begin X" / "BENCH end X" / "BENCH done" on stderr; the
-- runner uses them to cut the RTRACE lines into scenarios.

-- LEKTRA_BENCH_DARK=1: render with page colours and high contrast on, so the
-- tint and tone-stretch passes run (the defaults do neither).
if os.getenv("LEKTRA_BENCH_DARK") then
    lektra.opt.page.bg = "#1e1e2e"
    lektra.opt.page.fg = "#cdd6f4"
    lektra.opt.behavior.high_contrast = true
end

local function mark(text)
    io.stderr:write("BENCH " .. text .. "\n")
    io.stderr:flush()
end

local wanted = {}
for s in (os.getenv("LEKTRA_BENCH_SCENARIOS") or "A,B,C,D"):gmatch("[^,]+") do
    wanted[s:upper()] = true
end

local function settle(view, seconds)
    lektra.sleep(seconds or 2.0)
end

local scenarios = {}

function scenarios.A(view)
    view:set_layout(lektra.LayoutMode.Vertical)
    view:set_zoom(1.0)
    view:goto_page(1)
    settle(view, 1.5)
    local _, _, _, max_y = view:scroll_position()
    local step = 800
    for _ = 1, 200 do
        view:scroll(0, step)
        lektra.sleep(0.016)
    end
    mark("inputend")
    settle(view)
end

function scenarios.B(view)
    view:set_layout(lektra.LayoutMode.Vertical)
    view:set_zoom(1.0)
    view:goto_page(math.max(1, math.floor(view:page_count() / 2)))
    settle(view, 1.5)
    local z = 1.0
    for _ = 1, 30 do
        z = z * 1.1
        view:set_zoom(z)
        lektra.sleep(0.06)
    end
    mark("inputend")
    settle(view)
    view:set_zoom(1.0)
    settle(view, 1.0)
end

function scenarios.C(view)
    view:set_layout(lektra.LayoutMode.Vertical)
    view:goto_page(math.max(1, math.floor(view:page_count() / 2)))
    view:set_zoom(8.0)
    settle(view, 2.0)
    for i = 1, 100 do
        view:scroll((i % 2 == 0) and 300 or -300, 300)
        lektra.sleep(0.03)
    end
    mark("inputend")
    settle(view)
    view:set_zoom(1.0)
    settle(view, 1.0)
end

function scenarios.D(view)
    view:set_layout(lektra.LayoutMode.Vertical)
    view:set_zoom(1.0)
    view:goto_page(1)
    settle(view, 1.5)
    for _ = 1, 40 do
        view:scroll(0, 700)
        lektra.sleep(0.08)
    end
    mark("inputend")
    settle(view)
end

-- Waits (up to `seconds`) for a view whose document is open.
local function wait_for_view(seconds)
    local waited = 0
    while waited < seconds do
        local ok, v = pcall(lektra.view.current)
        if ok and v then
            local ok2, n = pcall(function() return v:page_count() end)
            if ok2 and n and n > 0 then
                return v
            end
        end
        lektra.sleep(0.2)
        waited = waited + 0.2
    end
    return nil
end

local started = false
local function start()
    if started then
        return
    end
    started = true
    mark("start")
    lektra.async(function()
        local v = wait_for_view(30)
        if not v then
            mark("error no document view became ready")
            mark("done")
            return
        end
        lektra.sleep(1.5) -- first page and the initial preload
        for _, name in ipairs({ "A", "B", "C", "D" }) do
            if wanted[name] then
                mark("begin " .. name)
                local ok, err = pcall(scenarios[name], v)
                if not ok then
                    mark("error scenario " .. name .. ": " .. tostring(err))
                end
                mark("end " .. name)
            end
        end
        mark("done")
    end)
end

-- Whichever fires first starts the run; wait_for_view covers the case that
-- the document is not open yet.
lektra.event.register("OnAppReady", start)
lektra.event.register("OnFileOpen", start)
lektra.event.register("OnReady", start)
