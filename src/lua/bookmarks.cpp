#include "Lektra.hpp"
#include "LuaBookmark.hpp"

void
Lektra::initLuaBookmarks() noexcept
{
    lua_newtable(m_L);

    // lektra.bookmarks.list() -> { {id, file_path, pageno, x, y, created}, ...
    // }
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        lua_newtable(L);
        int idx = 1;
        for (const auto &bookmark : lektra->m_bookmark_manager.bookmarks())
        {
            pushLuaBookmark(L, bookmark);
            lua_rawseti(L, -2, idx++);
        }
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "list");

    // lektra.bookmarks.add([{file_path=, pageno=, x=, y=}]) -> id
    // Without arguments, bookmarks the current location of the current
    // view. pageno is 1-based.
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        QString filePath;
        PageLocation location{};
        if (lektra->m_doc)
        {
            filePath = lektra->m_doc->filePath();
            location = lektra->m_doc->CurrentLocation();
        }

        if (lua_istable(L, 1))
        {
            lua_getfield(L, 1, "file_path");
            if (lua_isstring(L, -1))
                filePath = QString::fromUtf8(lua_tostring(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, 1, "pageno");
            if (lua_isnumber(L, -1))
                location.pageno = static_cast<int>(lua_tointeger(L, -1)) - 1;
            lua_pop(L, 1);

            lua_getfield(L, 1, "x");
            if (lua_isnumber(L, -1))
                location.x = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, 1, "y");
            if (lua_isnumber(L, -1))
                location.y = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }

        if (filePath.isEmpty())
            return luaL_error(L, "bookmarks.add: no file (open a document or "
                                 "pass file_path)");

        const Bookmark bookmark(filePath, location,
                                QDateTime::currentDateTime());
        lektra->addBookmark(bookmark);
        lua_pushstring(L, bookmark.id().toUtf8().constData());
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "add");

    lua_setfield(m_L, -2, "bookmarks");
}
