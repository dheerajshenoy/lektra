#include "Lektra.hpp"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>

// Pretty print anything, table or value, to a string
static std::string
inspect(lua_State *L, int index, int depth = 0) noexcept
{
    index = lua_absindex(L, index);
    std::string indent(depth * 2, ' ');
    std::string inner(depth * 2 + 2, ' ');

    switch (lua_type(L, index))
    {
        case LUA_TNIL:
            return "nil";
        case LUA_TBOOLEAN:
            return lua_toboolean(L, index) ? "true" : "false";
        case LUA_TNUMBER:
        {
            if (lua_isinteger(L, index))
                return std::to_string(lua_tointeger(L, index));
            // strip trailing zeros
            char buf[32];
            snprintf(buf, sizeof(buf), "%g", lua_tonumber(L, index));
            return buf;
        }
        case LUA_TSTRING:
            return std::string("\"") + lua_tostring(L, index) + "\"";
        case LUA_TTABLE:
        {
            std::string result = "{\n";

            // Array part first
            int arr_len = (int)lua_rawlen(L, index);
            for (int i = 1; i <= arr_len; i++)
            {
                lua_rawgeti(L, index, i);
                result += inner + inspect(L, -1, depth + 1) + ",\n";
                lua_pop(L, 1);
            }

            // Hash part
            lua_pushvalue(L, index);
            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                // Skip integer keys already covered by array part
                if (lua_isinteger(L, -2))
                {
                    lua_Integer k = lua_tointeger(L, -2);
                    if (k >= 1 && k <= arr_len)
                    {
                        lua_pop(L, 1);
                        continue;
                    }
                }

                lua_pushvalue(L, -2); // copy key
                std::string key = inspect(L, -1, depth + 1);
                lua_pop(L, 1);

                std::string val = inspect(L, -1, depth + 1);
                result += inner + key + " = " + val + ",\n";
                lua_pop(L, 1);
            }
            lua_pop(L, 1);

            return result + indent + "}";
        }
        case LUA_TFUNCTION:
            return "<function>";
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            return std::string("<userdata: 0x") + [](const void *p)
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "%p", p);
                return std::string(buf);
            }(lua_topointer(L, index)) + ">";
        default:
            return std::string("<") + luaL_typename(L, index) + ">";
    }
}

static int
l_print(lua_State *L)
{
    int n = lua_gettop(L); // number of arguments
    for (int i = 1; i <= n; i++)
    {
        // Use your inspect function here
        std::string s = inspect(L, i);
        std::cout << s << (i < n ? "\t" : "");
    }
    std::cout << std::endl;
    return 0;
}

void
Lektra::initLuaUtils() noexcept
{
    lua_newtable(m_L); // lektra.utils

    lua_pushcfunction(m_L, l_print);
    lua_setfield(m_L, -2, "print");

    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
        const char *url = luaL_checkstring(L, 1);
        QDesktopServices::openUrl(QUrl::fromUserInput(QString::fromUtf8(url)));
        return 0;
    });
    lua_setfield(m_L, -2, "open_url");

    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
#if defined(_WIN32)
        lua_pushstring(L, "windows");
#elif defined(__linux__)
        lua_pushstring(L, "linux");
#elif defined(__APPLE__)
        luaL_pushstring(L, "macos");
#else
        lua_pushstring(L, "unknown");
#endif
        return 1;
    });
    lua_setfield(m_L, -2, "platform");

    lua_setfield(m_L, -2, "utils");
}

// lektra.clipboard.get() -> string, lektra.clipboard.set(text)
void
Lektra::initLuaClipboard() noexcept
{
    lua_newtable(m_L);

    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        lua_pushstring(
            L, QGuiApplication::clipboard()->text().toUtf8().constData());
        return 1;
    }, 0);
    lua_setfield(m_L, -2, "get");

    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        size_t length    = 0;
        const char *text = luaL_checklstring(L, 1, &length);
        QGuiApplication::clipboard()->setText(
            QString::fromUtf8(text, static_cast<qsizetype>(length)));
        return 0;
    }, 0);
    lua_setfield(m_L, -2, "set");

    lua_setfield(m_L, -2, "clipboard");
}
