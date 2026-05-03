#include "Lektra.hpp"

void
Lektra::executeLuaCode(const QString &code) noexcept
{
    if (!m_L)
        return;

    // Get the output of the Lua code execution
    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
        const char *output = lua_tostring(L, 1);
        if (output)
        {
            qDebug() << "Lua Output:" << output;
        }
        return 0;
    });

    if (luaL_loadstring(m_L, code.toStdString().c_str()) != LUA_OK)
    {
        qWarning() << "Failed to load Lua code:" << lua_tostring(m_L, -1);
        lua_pop(m_L, 1); // Remove error message from stack
        return;
    }

    if (lua_pcall(m_L, 0, 0, -2) != LUA_OK)
    {
        qWarning() << "Failed to execute Lua code:" << lua_tostring(m_L, -1);
        lua_pop(m_L, 1); // Remove error message from stack
    }
}

void
Lektra::initLua() noexcept
{
    m_L = luaL_newstate();

    luaL_openlibs(m_L);

    lua_newtable(m_L); // "lektra" global table for organization

    // Register functions
    initLuaOpt();
    initLuaCmd();
    initLuaUI();
    initLuaTabs();
    initLuaDocumentView();
    initLuaEventDispatcher();
    initLuaView();
    initLuaKeymaps();
    initLuaMousemaps();

    lua_setglobal(m_L, "lektra");

    const QString init_file = m_config_dir.filePath("init.lua");
    if (QFile::exists(init_file))
    {
        if (luaL_dofile(m_L, init_file.toStdString().c_str()) != LUA_OK)
        {
            qWarning() << "Failed to execute init.lua:"
                       << lua_tostring(m_L, -1);
            lua_pop(m_L, 1); // Remove error message from stack
        }
    }
}
