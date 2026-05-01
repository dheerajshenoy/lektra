#include "Lektra.hpp"

static DispatchType
stringToDispatchType(const QString &name)
{
    static const std::unordered_map<QString, DispatchType> eventMap = {
        {"OnReady", DispatchType::OnReady},
        {"OnFileOpen", DispatchType::OnFileOpen},
        {"OnPageChanged", DispatchType::OnPageChanged},
        {"OnZoomChanged", DispatchType::OnZoomChanged},
        {"OnLinkClicked", DispatchType::OnLinkClicked},
        {"OnSelectionChanged", DispatchType::OnSelectionChanged},
        {"OnHistoryChanged", DispatchType::OnHistoryChanged},
        {"OnTabChanged", DispatchType::OnTabChanged},
        {"OnMarkSet", DispatchType::OnMarkSet},
        {"OnMarkDeleted", DispatchType::OnMarkDeleted},
        {"OnGotoMark", DispatchType::OnGotoMark},
        {"OnCommand", DispatchType::OnCommand},
    };

    if (!eventMap.contains(name))
        throw std::invalid_argument(
            QString("Unknown event name: %1").arg(name).toStdString());

    return eventMap.at(name);
}

void
Lektra::initLuaEventDispatcher() noexcept
{
    lua_newtable(m_L);

    // lektra.event.on(eventName, callback)
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

        auto &callbacks = self->m_lua_event_dispatcher[dtype];
        callbacks.push_back([L = self->m_L, callbackRef]()
        {
            // Push the callback function onto the stack
            lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRef);
            // Call the function with 0 arguments and 0 return values
            if (lua_pcall(L, 0, 0, 0) != LUA_OK)
            {
                qWarning() << "Error calling Lua callback for event:"
                           << lua_tostring(L, -1);
                lua_pop(L, 1); // Pop the error message
            }
        });

        return 0; // No return values
    }, 1);

    lua_setfield(m_L, -2, "on");    // lektra.event.on
    lua_setfield(m_L, -2, "event"); // lektra.event
}

void
Lektra::dispatchLuaEvent(DispatchType type) noexcept
{
    for (const auto &callback : m_lua_event_dispatcher[type])
        callback();
}
