#include "LuaSandbox.hpp"

#include "LuaCompat.hpp"

#if __has_include(<luajit.h>)
    #include <luajit.h>
#endif

#include <QElapsedTimer>
#include <QStringList>
#include <QDebug>
#include <cstring>

namespace
{
constexpr int kMaxOutputChars = 20000;
constexpr int kMaxTableEntries = 25;
constexpr int kMaxDepth = 3;

struct RunState
{
    QElapsedTimer timer;
    int limitMs = 3000;
    QString output;
    bool timedOut = false;
};

// Only touched on the GUI thread, where Lua runs.
RunState *g_run = nullptr;

void
appendOutput(const QString &text)
{
    if (!g_run)
    {
        qInfo().noquote() << "script:" << text; // from a callback after the run
        return;
    }
    if (g_run->output.size() >= kMaxOutputChars)
        return;
    g_run->output += text;
    if (g_run->output.size() > kMaxOutputChars)
    {
        g_run->output.truncate(kMaxOutputChars);
        g_run->output += QStringLiteral("\n... (output truncated)");
    }
}

QString
formatValue(lua_State *L, int idx, int depth = 0, bool quoteStrings = false)
{
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx))
    {
        case LUA_TNIL:
            return QStringLiteral("nil");
        case LUA_TBOOLEAN:
            return lua_toboolean(L, idx) ? QStringLiteral("true")
                                         : QStringLiteral("false");
        case LUA_TNUMBER:
            return QString::number(lua_tonumber(L, idx), 'g', 14);
        case LUA_TSTRING:
        {
            const QString s = QString::fromUtf8(lua_tostring(L, idx));
            return quoteStrings ? QStringLiteral("\"") + s + QStringLiteral("\"")
                                : s;
        }
        case LUA_TTABLE:
        {
            if (depth >= kMaxDepth)
                return QStringLiteral("{...}");
            QStringList parts;
            int count = 0;
            lua_pushnil(L);
            while (lua_next(L, idx) != 0)
            {
                if (++count > kMaxTableEntries)
                {
                    lua_pop(L, 2);
                    parts << QStringLiteral("...");
                    break;
                }
                // key at -2, value at -1
                QString key;
                if (lua_type(L, -2) == LUA_TSTRING)
                    key = QString::fromUtf8(lua_tostring(L, -2));
                else
                    key = QStringLiteral("[") + formatValue(L, -2, depth + 1, true)
                          + QStringLiteral("]");
                parts << key + QStringLiteral(" = ")
                             + formatValue(L, -1, depth + 1, true);
                lua_pop(L, 1);
            }
            return QStringLiteral("{ ") + parts.join(QStringLiteral(", "))
                   + QStringLiteral(" }");
        }
        default:
            return QStringLiteral("<%1>")
                .arg(QString::fromUtf8(luaL_typename(L, idx)));
    }
}

int
sandboxPrint(lua_State *L)
{
    const int n = lua_gettop(L);
    QStringList parts;
    for (int i = 1; i <= n; ++i)
        parts << formatValue(L, i);
    appendOutput(parts.join(QLatin1Char('\t')) + QLatin1Char('\n'));
    return 0;
}

void
timeoutHook(lua_State *L, lua_Debug *)
{
    if (!g_run || g_run->timer.elapsed() <= g_run->limitMs)
        return;
    // Once past the limit every further instruction raises, so a script that
    // swallows the error with pcall inside a loop is still stopped by the
    // instructions of its own loop.
    lua_sethook(L, timeoutHook, LUA_MASKCOUNT, 1);
    g_run->timedOut = true;
    luaL_error(L, "script took longer than %d ms and was stopped",
               g_run->limitMs);
}

// Shallow copy of global table `name` into field `name` of the table at -1.
void
copyGlobalTable(lua_State *L, const char *name, const char *const *skip = nullptr)
{
    lua_getglobal(L, name);
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }
    lua_newtable(L);
    lua_pushnil(L);
    while (lua_next(L, -3) != 0)
    {
        bool skipIt = false;
        if (skip && lua_type(L, -2) == LUA_TSTRING)
            for (const char *const *s = skip; *s; ++s)
                if (strcmp(*s, lua_tostring(L, -2)) == 0)
                    skipIt = true;
        if (skipIt)
            lua_pop(L, 1);
        else
        {
            lua_pushvalue(L, -2); // key
            lua_insert(L, -2);    // key, value -> key(copy) below value
            lua_settable(L, -4);  // new_table[key] = value
        }
    }
    lua_remove(L, -2);            // drop the original table
    lua_setfield(L, -2, name);    // env[name] = copy
}

// Pushes the restricted environment table.
void
pushEnvironment(lua_State *L)
{
    static const char *const kBasic[]
        = {"assert", "error",   "ipairs", "next",   "pairs",    "pcall",
           "select", "tonumber", "tostring", "type", "unpack",  "xpcall",
           "rawequal", "rawget", "rawset",  "setmetatable", nullptr};

    lua_newtable(L); // env

    for (const char *const *name = kBasic; *name; ++name)
    {
        lua_getglobal(L, *name);
        lua_setfield(L, -2, *name);
    }

    static const char *const kNoStringDump[] = {"dump", nullptr};
    copyGlobalTable(L, "string", kNoStringDump);
    copyGlobalTable(L, "table");
    copyGlobalTable(L, "math");
    copyGlobalTable(L, "bit");

    // os: time and date only
    lua_newtable(L);
    lua_getglobal(L, "os");
    if (lua_istable(L, -1))
    {
        for (const char *fn : {"time", "date", "clock", "difftime"})
        {
            lua_getfield(L, -1, fn);
            lua_setfield(L, -3, fn);
        }
    }
    lua_pop(L, 1);
    lua_setfield(L, -2, "os");

    // lektra, without job: the assistant must not be able to run commands
    // (os.execute is not available to it either)
    // no background commands, and no paths of the user's computer
    static const char *const kNoJob[] = {"job", "paths", "statusbar", "async", "await", "sleep", nullptr};
    copyGlobalTable(L, "lektra", kNoJob);

    lua_pushstring(L, "Lua 5.1");
    lua_setfield(L, -2, "_VERSION");
    lua_pushcfunction(L, sandboxPrint);
    lua_setfield(L, -2, "print");

    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_G");
}
} // namespace

LuaScriptResult
runSandboxedLua(lua_State *L, const QString &code, int timeLimitMs)
{
    LuaScriptResult result;
    const int top = lua_gettop(L);

    const QByteArray src = code.toUtf8();
    if (luaL_loadbuffer(L, src.constData(), static_cast<size_t>(src.size()),
                        "=script")
        != 0)
    {
        result.error = QString::fromUtf8(lua_tostring(L, -1));
        lua_settop(L, top);
        return result;
    }

    pushEnvironment(L);
    lua_setfenv(L, -2); // the chunk's globals are the restricted environment

    RunState state;
    state.limitMs = timeLimitMs;
    state.timer.start();
    g_run = &state;

#ifdef LUAJIT_VERSION
    // Compiled loops never call hooks, so a runaway loop would never reach
    // the time limit. Scripts from a model do not need the JIT: run them in
    // the interpreter, and turn the JIT back on afterwards.
    luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
#endif
    lua_sethook(L, timeoutHook, LUA_MASKCOUNT, 10000);

    const int status = lua_pcall(L, 0, LUA_MULTRET, 0);

    lua_sethook(L, nullptr, 0, 0);
#ifdef LUAJIT_VERSION
    luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
#endif
    g_run = nullptr;

    result.output = state.output;
    if (status != 0)
    {
        result.error = lua_isstring(L, -1)
                           ? QString::fromUtf8(lua_tostring(L, -1))
                           : QStringLiteral("(error object is a %1)")
                                 .arg(QString::fromUtf8(luaL_typename(L, -1)));
    }
    else
    {
        result.ok = true;
        QStringList values;
        for (int i = top + 1; i <= lua_gettop(L); ++i)
            values << formatValue(L, i, 0, true);
        result.value = values.join(QLatin1Char('\n'));
    }
    lua_settop(L, top);
    return result;
}
