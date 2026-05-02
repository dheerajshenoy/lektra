#include "Lektra.hpp"

void
Lektra::initLuaTabs() noexcept
{
    lua_newtable(m_L); // lektra.tabs table
    lua_pushlightuserdata(m_L, this);

    // lektra.tabs.close(index)
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        int index = luaL_optinteger(L, 1, -1);
        if (index < -1)
            return luaL_error(L, "Invalid tab index: %d", index);

        lektra->Tab_close(index);
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "close");

    // lektra.tabs.goto(index)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        int index = luaL_optinteger(L, 1, -1);
        if (index < -1)
            return luaL_error(L, "Invalid tab index: %d", index);

        lektra->Tab_goto(index);

        return 0;
    }, 1);
    lua_setfield(m_L, -2, "goto");

    // lektra.tabs.last()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->Tab_last();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "last");

    // lektra.tabs.first()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->Tab_first();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "first");

    // lektra.tabs.next()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->Tab_next();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "next");

    // lektra.tabs.prev()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->Tab_prev();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "prev");

    // lektra.tabs.move_right()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->TabMoveRight();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "move_right");

    // lektra.tabs.move_left()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->TabMoveLeft();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "move_left");

    // lektra.tabs.count()
    lua_pushlightuserdata(m_L, m_tab_widget);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *tab_widget
            = static_cast<TabWidget *>(lua_touserdata(L, lua_upvalueindex(1)));
        lua_pushinteger(L, tab_widget->count());
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "count");

    // lektra.tabs.current()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        int current_index
            = lektra->m_tab_widget ? lektra->m_tab_widget->currentIndex() : -1;
        lua_pushinteger(L, current_index);
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "current");

    // lektra.tabs.list() - returns a table of {index, title} for each tab
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        auto *tab_widget = lektra->m_tab_widget;
        if (!tab_widget)
        {
            lua_pushnil(L);
            return 1;
        }

        lua_newtable(L);
        for (int i = 0; i < tab_widget->count(); ++i)
        {
            lua_newtable(L);
            lua_pushinteger(L, i);
            lua_setfield(L, -2, "index");
            lua_pushstring(L, tab_widget->tabText(i).toUtf8().constData());
            lua_setfield(L, -2, "title");
            lua_rawseti(L, -2, i + 1);
        }

        return 1;
    }, 1);
    lua_setfield(m_L, -2, "list");

    lua_setfield(m_L, -2, "tabs");
}
