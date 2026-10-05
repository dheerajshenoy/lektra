#pragma once

struct lua_State;

// The Lua state of the main thread.
//
// A callback that is kept to be called later (a timer, a job, a dialog...)
// must run on this state, not on the `L` of the function that stored it: when
// that function was called from a coroutine (lektra.async), `L` is the
// coroutine's thread, which is suspended or gone by the time the callback
// runs.
inline lua_State *&
luaMainStateStorage() noexcept
{
    static lua_State *state = nullptr;
    return state;
}

inline lua_State *
luaMainState() noexcept
{
    return luaMainStateStorage();
}

inline void
setLuaMainState(lua_State *state) noexcept
{
    luaMainStateStorage() = state;
}
