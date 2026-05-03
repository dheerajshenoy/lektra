#include "Config.hpp"
#include "Lektra.hpp"

namespace
{
static void
registerKeymaps(lua_State *L, Config &config) noexcept
{
    lua_newtable(L); // lektra.keymap
    lua_pushlightuserdata(L, &config.keybinds);

    // lektra.keymap.set(name, value)
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        auto *keybinds = static_cast<QHash<QString, QString> *>(
            lua_touserdata(L, lua_upvalueindex(1)));

        if (lua_gettop(L) != 2 || !lua_isstring(L, 1) || !lua_isstring(L, 2))
        {
            return luaL_error(L, "Expected two string arguments: name, value");
        }

        const char *name  = lua_tostring(L, 1);
        const char *value = lua_tostring(L, 2);

        (*keybinds)[QString::fromUtf8(name)] = QString::fromUtf8(value);

        return 0; // No return values
    }, 1);

    lua_setfield(L, -2, "set");

    // lektra.keymap.unset(name)
    lua_pushlightuserdata(L, &config);
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        auto *keybinds = static_cast<QHash<QString, QString> *>(
            lua_touserdata(L, lua_upvalueindex(1)));

        if (lua_gettop(L) != 1 || !lua_isstring(L, 1))
        {
            return luaL_error(L, "Expected two string arguments: name, value");
        }

        const char *name = lua_tostring(L, 1);

        if (!keybinds->contains(QString::fromUtf8(name)))
        {
            return luaL_error(L, "Keybind not found: %s", name);
        }
        else
        {
            keybinds->remove(QString::fromUtf8(name));
        }

        return 0; // No return values
    }, 1);

    lua_setfield(L, -2, "unset");

    lua_setfield(L, -2, "keymap");
}
} // namespace

void
Lektra::initLuaKeymaps() noexcept
{
    registerKeymaps(m_L, m_config);
}
