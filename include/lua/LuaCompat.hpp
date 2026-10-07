#pragma once

// The bindings are written against the Lua 5.2+ C API. LuaJIT implements
// Lua 5.1 (plus a few 5.2 additions), so provide the handful of calls it
// lacks. Everything here is guarded so a newer Lua would use its own.

#include <cmath>
#include <lua.hpp>

#if LUA_VERSION_NUM < 502

// Absolute stack index for a relative one (pseudo-indices are unchanged).
inline int
lua_absindex(lua_State *L, int idx)
{
    return (idx > 0 || idx <= LUA_REGISTRYINDEX) ? idx
                                                 : lua_gettop(L) + idx + 1;
}

inline size_t
lua_rawlen(lua_State *L, int idx)
{
    return lua_objlen(L, idx);
}

inline lua_Integer
luaL_len(lua_State *L, int idx)
{
    return static_cast<lua_Integer>(lua_objlen(L, idx));
}

// LuaJIT has no integer subtype: a number counts as an integer when its
// value is whole (so 3 and 3.0 are the same, as they print in LuaJIT).
inline int
lua_isinteger(lua_State *L, int idx)
{
    if (lua_type(L, idx) != LUA_TNUMBER)
        return 0;
    const lua_Number n = lua_tonumber(L, idx);
    return std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 9.0e15;
}

#endif
