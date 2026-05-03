#include "Lektra.hpp"

namespace
{
static void
registerMousemaps(lua_State *L, Lektra *lektra) noexcept
{
    lua_newtable(L); // lektra.mousemap

    lua_pushlightuserdata(L, lektra);

    // lektra.mousemap.set(action, trigger: string)
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        if (lua_gettop(L) != 2 || !lua_isstring(L, 1) || !lua_isstring(L, 2))
        {
            return luaL_error(L,
                              "Expected two string arguments: action, trigger");
        }

        const char *action  = lua_tostring(L, 1);
        const char *trigger = lua_tostring(L, 2);

        lektra->setupMousebinding(QString::fromUtf8(action),
                                  QString::fromUtf8(trigger));
        return 0;
    }, 1);
    lua_setfield(L, -2, "set");

    // lektra.mousemap.unset(action)
    lua_pushlightuserdata(L, lektra);
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        if (lua_gettop(L) != 1 || !lua_isstring(L, 1))
        {
            return luaL_error(L, "Expected one string argument: action");
        }

        const char *action = lua_tostring(L, 1);

        lektra->unsetMousebinding(action);
        return 0;
    }, 1);
    lua_setfield(L, -2, "unset");

    lua_setfield(L, -2, "mousemap");
}
} // namespace

void
Lektra::initLuaMousemaps() noexcept
{
    registerMousemaps(m_L, this);
}
