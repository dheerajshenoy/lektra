#include "Lektra.hpp"

#include <QProcess>

// lektra.sessions, lektra.recent_files and lektra.window
void
Lektra::initLuaSessions() noexcept
{
    auto setFn = [this](const char *name, lua_CFunction fn)
    {
        lua_pushlightuserdata(m_L, this);
        lua_pushcclosure(m_L, fn, 1);
        lua_setfield(m_L, -2, name);
    };

    // ----------------------------------------------------------------- sessions
    lua_newtable(m_L);

    // lektra.sessions.list() -> string[]
    setFn("list", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        const QStringList names = lektra->getSessionFiles();
        lua_createtable(L, static_cast<int>(names.size()), 0);
        int i = 1;
        for (const QString &name : names)
        {
            lua_pushstring(L, name.toUtf8().constData());
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });

    // lektra.sessions.exists(name) -> boolean
    setFn("exists", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lua_pushboolean(L, lektra->sessionExists(
                               QString::fromUtf8(luaL_checkstring(L, 1))));
        return 1;
    });

    // lektra.sessions.load(name) -> boolean. Opens in a new window when this
    // one already has documents open, like the load_session command.
    setFn("load", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        const QString name = QString::fromUtf8(luaL_checkstring(L, 1));
        if (!lektra->sessionExists(name))
        {
            lua_pushboolean(L, 0);
            lua_pushstring(L, "no such session");
            return 2;
        }
        lektra->LoadSession(name);
        lua_pushboolean(L, 1);
        return 1;
    });

    // lektra.sessions.delete(name) -> boolean
    setFn("delete", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lua_pushboolean(L, lektra->deleteSession(
                               QString::fromUtf8(luaL_checkstring(L, 1))));
        return 1;
    });

    lua_setfield(m_L, -2, "sessions");

    // ------------------------------------------------------------ recent files
    lua_newtable(m_L);

    // lektra.recent_files.list() -> {file_path, page, last_accessed}[], newest first
    setFn("list", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        const auto &entries = lektra->m_recent_files_store.entries();
        lua_createtable(L, static_cast<int>(entries.size()), 0);
        int i = 1;
        for (const RecentFileEntry &entry : entries)
        {
            if (entry.file_path.isEmpty())
                continue;
            lua_createtable(L, 0, 3);
            lua_pushstring(L, entry.file_path.toUtf8().constData());
            lua_setfield(L, -2, "file_path");
            lua_pushinteger(L, entry.page_number);
            lua_setfield(L, -2, "page");
            lua_pushinteger(L, entry.last_accessed.toSecsSinceEpoch());
            lua_setfield(L, -2, "last_accessed");
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });

    lua_setfield(m_L, -2, "recent_files");

    // ------------------------------------------------------------------ window
    lua_newtable(m_L);

    // lektra.window.open([file]) -> boolean. Starts a new window, optionally
    // with a file open in it.
    setFn("open", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (lua_isnoneornil(L, 1))
        {
            lua_pushboolean(L, QProcess::startDetached(
                                   QCoreApplication::applicationFilePath(), {}));
            return 1;
        }
        lua_pushboolean(L, lektra->OpenFileInNewWindow(
                               QString::fromUtf8(luaL_checkstring(L, 1))));
        return 1;
    });

    lua_setfield(m_L, -2, "window");
}
