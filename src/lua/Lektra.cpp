#include "Lektra.hpp"

#include <QMessageBox>
#include <lua.h>

void
Lektra::executeLuaCode(const QString &code) noexcept
{
    if (!m_L)
        return;

    const int base            = lua_gettop(m_L);
    const QByteArray codeUtf8 = code.toUtf8();

    if (luaL_loadstring(m_L, codeUtf8.constData()) != LUA_OK)
    {
        qWarning() << "Failed to load Lua code:" << lua_tostring(m_L, -1);
        lua_settop(m_L, base);
        return;
    }

    if (lua_pcall(m_L, 0, 0, 0) != LUA_OK)
    {
        qWarning() << "Failed to execute Lua code:" << lua_tostring(m_L, -1);
        lua_settop(m_L, base);
    }
}

void
Lektra::initLuaLektra() noexcept
{
    // lektra.version
    lua_newtable(m_L);

    // lektra.version.full
    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
        lua_pushstring(L, APP_VERSION);
        return 1;
    });
    lua_setfield(m_L, -2, "full");

    // lektra.version.major
    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
        lua_pushstring(L, APP_VERSION_MAJOR);
        return 1;
    });
    lua_setfield(m_L, -2, "major");

    // lektra.version.minor
    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
        lua_pushstring(L, APP_VERSION_MINOR);
        return 1;
    });
    lua_setfield(m_L, -2, "minor");

    // lektra.version.patch
    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
        lua_pushstring(L, APP_VERSION_PATCH);
        return 1;
    });
    lua_setfield(m_L, -2, "patch");

    lua_setfield(m_L, -2, "version");

    // lektra.capabilities
    lua_newtable(m_L);

    // lektra.capabilities.synctex
    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
#ifdef WITH_SYNCTEX
        lua_pushboolean(L, true);
#else
        lua_pushboolean(L, false);
#endif
        return 1;
    });
    lua_setfield(m_L, -2, "synctex");

    // lektra.capabilities.djvu
    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
#ifdef WITH_DJVU
        lua_pushboolean(L, true);
#else
        lua_pushboolean(L, false);
#endif
        return 1;
    });
    lua_setfield(m_L, -2, "djvu");

    // lektra.capabilities.image
    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
#ifdef WITH_IMAGE
        lua_pushboolean(L, true);
#else
        lua_pushboolean(L, false);
#endif
        return 1;
    });
    lua_setfield(m_L, -2, "image");

    lua_setfield(m_L, -2, "capabilities");
}

void
Lektra::initLua() noexcept
{
    m_L = luaL_newstate();

    luaL_openlibs(m_L);

    lua_newtable(m_L); // "lektra" global table for organization

    // Register functions
    initLuaLektra();
    initLuaOpt();
    initLuaCmd();
    initLuaUI();
    initLuaTabs();
    initLuaEventDispatcher();
    initLuaView();
    initLuaKeymaps();
    initLuaMousemaps();
    initLuaUtils();
    initLuaBookmarks();

    lua_setglobal(m_L, "lektra");

    loadLuaConfig();
}
