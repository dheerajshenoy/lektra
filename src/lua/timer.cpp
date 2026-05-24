#include "Lektra.hpp"

#include <QTimer>

namespace
{

struct LuaTimer
{
    QTimer *timer        = nullptr;
    int     callback_ref = LUA_NOREF;
};

static LuaTimer *
checkTimer(lua_State *L) noexcept
{
    return static_cast<LuaTimer *>(luaL_checkudata(L, 1, "TimerMetaTable"));
}

static void
destroyTimer(lua_State *L, LuaTimer *ud) noexcept
{
    if (ud->timer)
    {
        ud->timer->deleteLater();
        ud->timer = nullptr;
    }
    if (ud->callback_ref != LUA_NOREF)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ud->callback_ref);
        ud->callback_ref = LUA_NOREF;
    }
}

static const luaL_Reg TimerMethods[] = {
    {"start",
     [](lua_State *L) -> int
     {
         LuaTimer *ud = checkTimer(L);
         if (ud->timer)
             ud->timer->start();
         return 0;
     }},

    {"stop",
     [](lua_State *L) -> int
     {
         LuaTimer *ud = checkTimer(L);
         if (ud->timer)
             ud->timer->stop();
         return 0;
     }},

    {"set_interval",
     [](lua_State *L) -> int
     {
         LuaTimer *ud = checkTimer(L);
         int ms       = static_cast<int>(luaL_checkinteger(L, 2));
         if (ud->timer)
             ud->timer->setInterval(ms);
         return 0;
     }},

    {"set_single_shot",
     [](lua_State *L) -> int
     {
         LuaTimer *ud = checkTimer(L);
         luaL_checktype(L, 2, LUA_TBOOLEAN);
         if (ud->timer)
             ud->timer->setSingleShot(lua_toboolean(L, 2));
         return 0;
     }},

    {"is_active",
     [](lua_State *L) -> int
     {
         LuaTimer *ud = checkTimer(L);
         lua_pushboolean(L, ud->timer && ud->timer->isActive());
         return 1;
     }},

    {"is_single_shot",
     [](lua_State *L) -> int
     {
         LuaTimer *ud = checkTimer(L);
         lua_pushboolean(L, ud->timer && ud->timer->isSingleShot());
         return 1;
     }},

    {"interval",
     [](lua_State *L) -> int
     {
         LuaTimer *ud = checkTimer(L);
         lua_pushinteger(L, ud->timer ? ud->timer->interval() : 0);
         return 1;
     }},

    {"destroy",
     [](lua_State *L) -> int
     {
         destroyTimer(L, checkTimer(L));
         return 0;
     }},

    {"__gc",
     [](lua_State *L) -> int
     {
         destroyTimer(L, checkTimer(L));
         return 0;
     }},

    {nullptr, nullptr}};

static void
registerTimerMetatable(lua_State *L)
{
    luaL_newmetatable(L, "TimerMetaTable");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, TimerMethods, 0);
    lua_pop(L, 1);
}

} // namespace

void
Lektra::initLuaTimer() noexcept
{
    registerTimerMetatable(m_L);

    lua_newtable(m_L);

    // lektra.timer.new(interval_ms, callback [, single_shot]) -> timer
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(
        m_L,
        [](lua_State *L) -> int
        {
            auto *self = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

            int ms = static_cast<int>(luaL_checkinteger(L, 1));
            luaL_checktype(L, 2, LUA_TFUNCTION);
            bool single_shot = lua_isnoneornil(L, 3) ? false : lua_toboolean(L, 3);

            // Pop the function and store it in the registry before creating the
            // timer so the ref is valid when the timeout lambda captures it.
            lua_pushvalue(L, 2);
            int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

            auto *ud    = static_cast<LuaTimer *>(lua_newuserdata(L, sizeof(LuaTimer)));
            ud->timer        = nullptr;
            ud->callback_ref = LUA_NOREF;

            luaL_getmetatable(L, "TimerMetaTable");
            lua_setmetatable(L, -2);

            auto *timer = new QTimer(self);
            timer->setInterval(ms);
            timer->setSingleShot(single_shot);

            QObject::connect(timer, &QTimer::timeout, self,
                             [L = self->m_L, cb_ref]()
            {
                lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);
                if (lua_pcall(L, 0, 0, 0) != LUA_OK)
                {
                    fprintf(stderr, "Lua error in timer callback: %s\n",
                            lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            });

            ud->timer        = timer;
            ud->callback_ref = cb_ref;

            return 1;
        },
        1);
    lua_setfield(m_L, -2, "new");

    lua_setfield(m_L, -2, "timer");
}
