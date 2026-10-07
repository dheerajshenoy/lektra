#pragma once

#include <QString>

struct lua_State;

// Result of running a script in the restricted environment.
struct LuaScriptResult
{
    bool ok = false;
    QString output; // everything the script print()ed
    QString value;  // values the script returned, formatted
    QString error;  // message when !ok
};

// Runs `code` on `L` in a restricted environment, for scripts written by a
// language model (or anyone else not trusted like init.lua is).
//
// The environment has `lektra` (so the script can do what it was asked to),
// the basic safe functions (pairs, pcall, tostring, ...), and copies of
// string, table, math, bit and os.{time,date,clock,difftime}. It has no io,
// os.execute/getenv/remove, require, load*, dofile, debug, ffi, jit,
// coroutine or getmetatable. print() is captured into `output`.
//
// The script is stopped with an error after `timeLimitMs` so a runaway loop
// cannot freeze the application. Callbacks the script registers with
// lektra.event or lektra.timer outlive the run and are not time-limited.
LuaScriptResult
runSandboxedLua(lua_State *L, const QString &code, int timeLimitMs = 3000);
