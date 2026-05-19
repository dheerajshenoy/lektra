#include "DispatchType.hpp"
#include "Lektra.hpp"

#include <QScreen>

bool
Lektra::removeLuaEventCallback(DispatchType type, int callbackRef) noexcept
{
    auto it = m_lua_event_dispatcher.find(type);
    if (it == m_lua_event_dispatcher.end())
        return false;

    auto &callbacks   = it->second;
    auto originalSize = callbacks.size();

    callbacks.erase(
        std::remove_if(callbacks.begin(), callbacks.end(),
                       [this, callbackRef](const LuaCallback<void> &cb)
    {
        if (cb.ref == callbackRef)
        {
            luaL_unref(m_L, LUA_REGISTRYINDEX, cb.ref);
            return true;
        }
        return false;
    }),
        callbacks.end());

    return callbacks.size() < originalSize;
}

static void
push_event_arg(lua_State *L, DispatchType type, void *data)
{
    switch (type)
    {
    case DispatchType::OnScreenChanged:
    {
        auto *screen = static_cast<QScreen *>(data);
        lua_newtable(L);
        lua_pushstring(L, screen->name().toUtf8().constData());
        lua_setfield(L, -2, "name");
        lua_pushnumber(L, screen->devicePixelRatio());
        lua_setfield(L, -2, "dpr");
        lua_pushnumber(L, screen->logicalDotsPerInch());
        lua_setfield(L, -2, "logical_dpi");
        lua_pushnumber(L, screen->physicalDotsPerInch());
        lua_setfield(L, -2, "physical_dpi");
        lua_pushnumber(L, screen->refreshRate());
        lua_setfield(L, -2, "refresh_rate");
        QRect g = screen->geometry();
        lua_newtable(L);
        lua_pushinteger(L, g.x());     lua_setfield(L, -2, "x");
        lua_pushinteger(L, g.y());     lua_setfield(L, -2, "y");
        lua_pushinteger(L, g.width()); lua_setfield(L, -2, "w");
        lua_pushinteger(L, g.height()); lua_setfield(L, -2, "h");
        lua_setfield(L, -2, "geometry");
        break;
    }
    case DispatchType::OnTabChanged:
    case DispatchType::OnTabRemoved:
        lua_pushinteger(L, *static_cast<int *>(data));
        break;
    default:
        if (data)
            lua_pushlightuserdata(L, data);
        else
            lua_pushnil(L);
        break;
    }
}

void
Lektra::initLuaEventDispatcher() noexcept
{
    lua_newtable(m_L);

    // lektra.event.register(EventType, callback) -> handle (int)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        DispatchType type = static_cast<DispatchType>(luaL_checkinteger(L, 1));
        luaL_checktype(L, 2, LUA_TFUNCTION);

        int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

        auto *self
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        self->addEventListener(type, callbackRef, false,
                               [L = self->m_L, callbackRef, type](void *data)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRef);
            push_event_arg(L, type, data);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {
                const char *errorMsg = lua_tostring(L, -1);
                fprintf(stderr, "Lua error in event callback: %s\n", errorMsg);
                lua_pop(L, 1);
            }
        });

        lua_pushinteger(L, callbackRef);
        return 1;
    }, 1);

    lua_setfield(m_L, -2, "register");

    // lektra.event.unregister(EventType, handle)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        DispatchType type = static_cast<DispatchType>(luaL_checkinteger(L, 1));
        int handle        = luaL_checkinteger(L, 2);

        auto *self
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        self->removeLuaEventCallback(type, handle);

        return 0;
    }, 1);

    lua_setfield(m_L, -2, "unregister");

    // lektra.event.once(EventType, callback)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        DispatchType type = static_cast<DispatchType>(luaL_checkinteger(L, 1));
        luaL_checktype(L, 2, LUA_TFUNCTION);

        int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

        auto *self
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        self->addEventListener(type, callbackRef, true,
                               [L = self->m_L, callbackRef, type](void *data)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRef);
            push_event_arg(L, type, data);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {
                const char *errorMsg = lua_tostring(L, -1);
                fprintf(stderr, "Lua error in event callback: %s\n", errorMsg);
                lua_pop(L, 1);
            }
            // Ref is freed by dispatchLuaEvent after all once-callbacks fire
        });

        lua_pushinteger(L, callbackRef);
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "once");

    // lektra.event.EventType enum (excludes COUNT sentinel)
    lua_newtable(m_L);

    for (int event = 0; event < static_cast<int>(DispatchType::COUNT); ++event)
    {
        lua_pushinteger(m_L, event);
        lua_setfield(m_L, -2,
                     dispatchTypeToString(static_cast<DispatchType>(event))
                         .toStdString()
                         .c_str());
    }
    lua_setfield(m_L, -2, "EventType");

    // lektra.event.count(EventType) -> int
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        DispatchType type = static_cast<DispatchType>(luaL_checkinteger(L, 1));
        auto *self
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        auto it = self->m_lua_event_dispatcher.find(type);
        if (it == self->m_lua_event_dispatcher.end())
        {
            lua_pushinteger(L, 0);
            return 1;
        }

        lua_pushinteger(L, it->second.size());
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "count");

    lua_setfield(m_L, -2, "event");
}

void
Lektra::dispatchLuaEvent(DispatchType type, void *arg) noexcept
{
    // Copy so callbacks can safely call unregister without invalidating
    // iteration
    auto callbacks = m_lua_event_dispatcher[type];
    for (const auto &callback : callbacks)
        callback.invoker(arg);

    // Remove and free any once-callbacks that just fired
    auto &live = m_lua_event_dispatcher[type];
    live.erase(std::remove_if(live.begin(), live.end(),
                              [this](const LuaCallback<void> &cb)
    {
        if (cb.is_once)
        {
            luaL_unref(m_L, LUA_REGISTRYINDEX, cb.ref);
            return true;
        }
        return false;
    }),
               live.end());
}
