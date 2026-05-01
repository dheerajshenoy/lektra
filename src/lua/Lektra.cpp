#include "Lektra.hpp"

#include "api.cpp"
#include "event.cpp"
#include "opt.cpp"
#include "ui.cpp"

void
Lektra::initLua() noexcept
{
    m_L = luaL_newstate();

    luaL_openlibs(m_L);

    lua_newtable(m_L); // "lektra" global table for organization

    // Register functions
    initLuaOpt();
    initLuaAPI();
    initLuaUI();
    initLuaEventDispatcher();

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
