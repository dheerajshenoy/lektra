#include "Config.hpp"
#include "Lektra.hpp"

namespace
{
static void
registerKeymaps(lua_State *L, Lektra *lektra) noexcept
{
    lua_newtable(L); // lektra.keymap
    lua_pushlightuserdata(L, lektra);

    // lektra.keymap.set(command, keys: table[string])
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        if (lua_gettop(L) != 2 || !lua_isstring(L, 1) || !lua_istable(L, 2))
        {
            return luaL_error(
                L, "Expected arguments: command (string), keys (table)");
        }

        const char *command = lua_tostring(L, 1);
        QStringList keys;

        // Iterate over the keys table
        // Lua tables are 1-indexed
        for (int i = 1; (lua_rawgeti(L, 2, i), !lua_isnil(L, -1)); ++i)
        {
            if (!lua_isstring(L, -1))
            {
                return luaL_error(L, "Keys table must contain only strings");
            }

            keys.append(QString::fromUtf8(lua_tostring(L, -1)));
            lua_pop(L, 1);
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

    // lektra.keymap.get(command) -> keys: table[string]
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
        QStringList keys = lektra->getKeybindings(QString::fromUtf8(command));

        lua_newtable(L); // Create a new table for the keys
        int index = 1;   // Lua tables are 1-indexed
        for (const QString &key : keys)
        {
            lua_pushstring(L, key.toUtf8().constData());
            lua_rawseti(L, -2, index++);
        }

        return 1; // Return the table of keys
    }, 1);
    lua_setfield(L, -2, "get");

    lua_setfield(L, -2, "keymap");
}
} // namespace

void
Lektra::initLuaKeymaps() noexcept
{
    registerKeymaps(m_L, this);
}
