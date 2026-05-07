#include "DispatchType.hpp"
#include "Lektra.hpp"

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
                       [this, callbackRef](const LuaCallback<Lektra> &cb)
    {
        if (cb.ref == callbackRef)
        {
            // Free the Lua registry reference
            luaL_unref(m_L, LUA_REGISTRYINDEX, cb.ref);
            return true;
        }
        return false;
    }),
        callbacks.end());

    return callbacks.size() < originalSize;
}

void
Lektra::initLuaEventDispatcher() noexcept
{
    lua_newtable(m_L);

    // lektra.event.register(event, callback) -> return an handle (int)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        DispatchType type = static_cast<DispatchType>(luaL_checkinteger(L, 1));
        luaL_checktype(L, 2, LUA_TFUNCTION);

        // Store the callback in the registry with a unique key
        int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

        auto *self
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        self->addEventListener(type, callbackRef, false, [L = self->m_L, callbackRef](Lektra *lektra)
        {            // Push the callback function onto the stack
            lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRef);

            // Call the function with the Lektra instance as an argument
            lua_pushlightuserdata(L, lektra);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {                // Handle Lua errors (e.g., print to console)
                const char *errorMsg = lua_tostring(L, -1);
                fprintf(stderr, "Lua error in event callback: %s\n", errorMsg);
                lua_pop(L, 1); // Remove error message from stack
            }
        });

        // Return the callback reference as a handle for potential
        // unregistration
        lua_pushinteger(L, callbackRef);
        return 1; // One return value (the handle)
    }, 1);

    lua_setfield(m_L, -2, "register"); // lektra.event.register

    // lektra.event.unregister(handle)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        const char *eventName = luaL_checkstring(L, 1);
        luaL_checktype(L, 2, LUA_TFUNCTION);

        int callbackRef = luaL_checkinteger(L, 2);
        auto *self
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        DispatchType dtype;
        try
        {
            dtype = stringToDispatchType(eventName);
        }
        catch (const std::invalid_argument &e)
        {
            luaL_error(L, e.what());
            return 0;
        }

        // Unregister the callback by removing it from the registry
        luaL_unref(L, LUA_REGISTRYINDEX, callbackRef);

        int handle = luaL_checkinteger(L, 1);

        self->removeLuaEventCallback(dtype, handle);

        return 0; // No return values
    }, 1);

    lua_setfield(m_L, -2, "unregister"); // lektra.event.unregister

    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        const char *eventName = luaL_checkstring(L, 1);
        luaL_checktype(L, 2, LUA_TFUNCTION);

        // Store the callback in the registry with a unique key
        int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

        auto *self
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        // Add the callback to our dispatcher map
        DispatchType dtype;
        try
        {
            dtype = stringToDispatchType(eventName);
        }
        catch (const std::invalid_argument &e)
        {
            luaL_error(L, e.what());
            return 0;
        }

        self->addEventListener(dtype, callbackRef, true, [L = self->m_L, callbackRef](Lektra *lektra)
        {            // Push the callback function onto the stack
            lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRef);

            // Call the function with the Lektra instance as an argument
            lua_pushlightuserdata(L, lektra);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {                // Handle Lua errors (e.g., print to console)
                const char *errorMsg = lua_tostring(L, -1);
                fprintf(stderr, "Lua error in event callback: %s\n", errorMsg);
                lua_pop(L, 1); // Remove error message from stack
            }

            // Since this is a "once" callback, we need to unregister it after execution
            luaL_unref(L, LUA_REGISTRYINDEX, callbackRef);
        });

        return 0; // No return values
    }, 1);
    lua_setfield(m_L, -2, "once"); // lektra.event.once

    // lektra.event.EventType enum
    lua_newtable(m_L);

    for (int event = 0; event <= static_cast<int>(DispatchType::COUNT); ++event)
    {
        lua_pushinteger(m_L, event);
        lua_setfield(m_L, -2,
                     dispatchTypeToString(static_cast<DispatchType>(event))
                         .toStdString()
                         .c_str());
    }
    lua_setfield(m_L, -2, "EventType");

    // lektra.event.count(name) -> int
    // Returns the number of registered callbacks for a given event type
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        const char *eventName = luaL_checkstring(L, 1);

        auto *self
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        DispatchType dtype;
        try
        {
            dtype = stringToDispatchType(eventName);
        }
        catch (const std::invalid_argument &e)
        {
            luaL_error(L, e.what());
            return 0;
        }

        auto it = self->m_lua_event_dispatcher.find(dtype);
        if (it == self->m_lua_event_dispatcher.end())
        {
            lua_pushinteger(L, 0);
            return 1; // One return value (the count)
        }

        lua_pushinteger(L, it->second.size());
        return 1; // One return value (the count)
    }, 1);
    lua_setfield(m_L, -2, "count");

    lua_setfield(m_L, -2, "event"); // lektra.event
                                    //
}

void
Lektra::dispatchLuaEvent(DispatchType type) noexcept
{
    for (const auto &callback : m_lua_event_dispatcher[type])
        callback.invoker(this);
}
