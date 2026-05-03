#include "Config.hpp"
#include "Lektra.hpp"

namespace
{
static void
registerKeymaps(lua_State *L, Lektra *lektra) noexcept
{
    lua_newtable(L); // lektra.keymap
    lua_pushlightuserdata(L, lektra);

    // lektra.keymap.set(command, keys: table[string] | string)
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        if (lua_gettop(L) != 2 || !lua_isstring(L, 1)
            || (!lua_istable(L, 2) && !lua_isstring(L, 2)))
        {
            return luaL_error(L,
                              "Expected two arguments: command (string), keys "
                              "(string or table of strings)");
        }

        const char *command = lua_tostring(L, 1);
        QStringList keys;

        if (lua_isstring(L, 2))
        {
            keys.append(QString::fromUtf8(lua_tostring(L, 2)));
        }
        else
        {
            // Iterate over the table and extract strings
            lua_pushnil(L); // First key
            while (lua_next(L, 2) != 0)
            {
                if (lua_isstring(L, -1))
                {
                    keys.append(QString::fromUtf8(lua_tostring(L, -1)));
                }
                else
                {
                    return luaL_error(L,
                                      "Expected string values in keys table");
                }
                lua_pop(L, 1); // Remove value, keep key for next iteration
            }
        }

        lektra->setupKeybinding(QString::fromUtf8(command), keys);

        return 0; // No return values
    }, 1);

    lua_setfield(L, -2, "set");

    // lektra.keymap.unset(command)
    lua_pushlightuserdata(L, lektra);
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        if (lua_gettop(L) != 1 || !lua_isstring(L, 1))
        {
            return luaL_error(L, "Expected string argument: command");
        }

        const char *command = lua_tostring(L, 1);

        lektra->unsetKeybinding(QString::fromUtf8(command));

        return 0; // No return values
    }, 1);

    lua_setfield(L, -2, "unset");

    lua_setfield(L, -2, "keymap");
}
} // namespace

void
Lektra::initLuaKeymaps() noexcept
{
    registerKeymaps(m_L, this);
}
