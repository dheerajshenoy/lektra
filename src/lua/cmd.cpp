#include "lua/MainState.hpp"
#include "Lektra.hpp"

void
Lektra::initLuaCmd() noexcept
{
    lua_newtable(m_L);

    // lektra.cmd.register(name, callback [, desc])
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        const char *name = nullptr;
        const char *desc = "";
        int func_idx     = -1;

        if (lua_istable(L, 1))
        {
            // ── Table form: register({ name=, callback=, desc=
            // }) ──
            lua_getfield(L, 1, "name");
            if (!lua_isstring(L, -1))
                return luaL_error(L, "register: 'name' must be a string");
            name = lua_tostring(L, -1);
            // leave on stack so pointer stays valid until reg() call
            // below

            lua_getfield(L, 1, "desc");
            if (lua_isstring(L, -1))
                desc = lua_tostring(L, -1);
            // leave on stack for same reason

            lua_getfield(L, 1, "callback");
            if (!lua_isfunction(L, -1))
                return luaL_error(L, "register: 'callback' must be a function");
            func_idx = lua_gettop(L); // callback is now at top
        }
        else if (lua_isstring(L, 1))
        {
            // ── Positional form: register(name, callback [,
            // desc]) ──
            name = luaL_checkstring(L, 1);
            if (!lua_isfunction(L, 2))
                return luaL_error(L,
                                  "register: argument 2 must "
                                  "be a function, got %s",
                                  luaL_typename(L, 2));
            func_idx = 2;
            desc     = luaL_optstring(L, 3, "");
        }
        else
        {
            return luaL_error(L, "register: expected string or table, got %s",
                              luaL_typename(L, 1));
        }

        lua_pushvalue(L, func_idx);
        const int func_ref = luaL_ref(L, LUA_REGISTRYINDEX);

        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        // Guard ensures luaL_unref is called when the command is removed.
        // Must use an explicit constructor so make_shared constructs in-place
        // with no temporary — a brace-init temporary would call ~LuaRefGuard
        // immediately after the move, freeing the ref before the command runs.
        struct LuaRefGuard
        {
            lua_State *L;
            int ref;
            LuaRefGuard(lua_State *l, int r) : L(l), ref(r) {}
            ~LuaRefGuard() { luaL_unref(L, LUA_REGISTRYINDEX, ref); }
        };
        auto guard = std::make_shared<LuaRefGuard>(luaMainState(), func_ref);

        lektra->commandManager()->reg(name, desc,
                                      [guard](const QStringList &args)
        {
            lua_rawgeti(guard->L, LUA_REGISTRYINDEX, guard->ref);
            lua_newtable(guard->L);
            int i = 1;

            for (const auto &arg : args)
            {
                lua_pushstring(guard->L, arg.toUtf8().constData());
                lua_rawseti(guard->L, -2, i++);
            }

            if (lua_pcall(guard->L, 1, 0, 0) != LUA_OK)
            {
                qWarning() << "register error:" << lua_tostring(guard->L, -1);
                lua_pop(guard->L, 1);
            }
        });

        return 0;
    }, 1);
    lua_setfield(m_L, -2, "register");

    // lektra.cmd.unregister(name)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        const char *name = luaL_checkstring(L, 1);

        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        lektra->commandManager()->unreg(name);

        return 0;
    }, 1);
    lua_setfield(m_L, -2, "unregister");

    // lektra.cmd.execute(name, [args]) -> bool (success)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        const char *name = luaL_checkstring(L, 1);
        QStringList args;
        if (lua_istable(L, 2))
        {
            for (lua_pushnil(L); lua_next(L, 2) != 0; lua_pop(L, 1))
            {
                if (lua_isstring(L, -1))
                    args << lua_tostring(L, -1);
                else
                    return luaL_error(
                        L, "execute: all args must be strings, got %s",
                        luaL_typename(L, -1));
            }
        }

        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        bool state = lektra->commandManager()->execute(name, args);

        lua_pushboolean(L, state);

        return 1;
    }, 1);
    lua_setfield(m_L, -2, "execute");

    // lektra.cmd.list() -> (CommandEntry){ name=..., desc=...}
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        lua_newtable(L);
        int i = 1;
        for (const auto &cmd : lektra->commandManager()->const_commands())
        {
            lua_newtable(L);
            lua_pushstring(L, cmd.name.toUtf8().constData());
            lua_setfield(L, -2, "name");
            lua_pushstring(L, cmd.description.toUtf8().constData());
            lua_setfield(L, -2, "desc");
            lua_rawseti(L, -2, i++);
        }
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "list");

    // lektra.cmd.alias(name, target)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        const char *name   = luaL_checkstring(L, 1);
        const char *target = luaL_checkstring(L, 2);

        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        lektra->commandManager()->alias(name, target);

        return 0;
    }, 1);
    lua_setfield(m_L, -2, "alias");

    lua_setfield(m_L, -2,
                 "cmd"); // lektra.api = the table we just built
}
