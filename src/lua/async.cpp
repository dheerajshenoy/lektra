#include "Lektra.hpp"

// lektra.async / lektra.await / lektra.sleep / lektra.job.await
//
// Coroutines on top of the callback functions, so that a script that does
// several slow things in a row reads from top to bottom:
//
//   lektra.async(function()
//       local r = lektra.job.await({"tesseract", png, base})
//       if not r.ok then return end
//       lektra.sleep(1)
//       lektra.ui.message("done")
//   end)
//
// The chunk below is plain Lua; it gets the `lektra` table as its argument.
namespace
{
constexpr const char *kAsyncChunk = R"LUA(
local lektra = ...
local unpack = unpack or table.unpack

-- Runs the coroutine up to its next await (or its end); an error in it is
-- printed, as the other callbacks do, and ends it.
local function step(co, ...)
    local ok, err = coroutine.resume(co, ...)
    if not ok then
        io.stderr:write("Lua error in lektra.async: ",
                        debug.traceback(co, tostring(err)), "\n")
    end
end

-- lektra.async(fn, ...): runs fn in a coroutine, starting at once and going on
-- until its first await. Returns the coroutine.
function lektra.async(fn, ...)
    if type(fn) ~= "function" then
        error("lektra.async: a function is expected", 2)
    end
    local co = coroutine.create(fn)
    step(co, ...)
    return co
end

-- lektra.await(starter): pauses the running lektra.async function until the
-- `resume` that starter(resume) was given is called, and returns what that
-- was called with. Use it for any callback-based function.
function lektra.await(starter)
    if type(starter) ~= "function" then
        error("lektra.await: a function is expected", 2)
    end
    -- (LuaJIT also returns the main thread here, with a second value true)
    local co, isMain = coroutine.running()
    if not co or isMain then
        error("lektra.await can only be used inside lektra.async", 2)
    end

    local used, waiting, early = false, false, nil
    starter(function(...)
        if used then return end -- only the first call counts
        used = true
        if waiting then
            step(co, ...)
        else
            early = { n = select("#", ...), ... } -- called before we paused
        end
    end)

    if early then
        return unpack(early, 1, early.n)
    end
    waiting = true
    return coroutine.yield()
end

-- lektra.job.await(command, [opts]) -> result: lektra.job.async, waited for.
function lektra.job.await(command, opts)
    return lektra.await(function(resume)
        lektra.job.async(command, opts or {}, resume)
    end)
end

-- lektra.sleep(seconds): waits, and the window stays responsive.
local sleepers = {}
function lektra.sleep(seconds)
    seconds = tonumber(seconds) or 0
    return lektra.await(function(resume)
        local timer
        timer = lektra.timer.new(math.max(1, math.floor(seconds * 1000)), function()
            sleepers[timer] = nil
            timer:destroy()
            resume()
        end, true)
        sleepers[timer] = true -- or it could be collected before it fires
        timer:start()
    end)
end
)LUA";
} // namespace

// Leaves the `lektra` table on the stack as it found it.
void
Lektra::initLuaAsync() noexcept
{
    if (luaL_loadstring(m_L, kAsyncChunk) != LUA_OK)
    {
        fprintf(stderr, "lektra.async: %s\n", lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return;
    }
    lua_pushvalue(m_L, -2); // the lektra table
    if (lua_pcall(m_L, 1, 0, 0) != LUA_OK)
    {
        fprintf(stderr, "lektra.async: %s\n", lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
    }
}
