#pragma once

#include "Bookmark.hpp"
#include "LuaCompat.hpp"

// Pushes a bookmark as a Lua table: { id, file_path, pageno (1-based), x, y,
// created }.
inline void
pushLuaBookmark(lua_State *L, const Bookmark &bookmark)
{
    lua_newtable(L);

    lua_pushstring(L, bookmark.id().toUtf8().constData());
    lua_setfield(L, -2, "id");

    lua_pushstring(L, bookmark.filePath().toUtf8().constData());
    lua_setfield(L, -2, "file_path");

    const auto location = bookmark.location();
    lua_pushinteger(L, location.pageno + 1);
    lua_setfield(L, -2, "pageno");
    lua_pushnumber(L, location.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, location.y);
    lua_setfield(L, -2, "y");

    lua_pushstring(L, bookmark.createdAt().toString().toUtf8().constData());
    lua_setfield(L, -2, "created");
}
