#include "Lektra.hpp"

namespace
{
static void
registerMousemaps(lua_State *L, Config &config) noexcept
{
    lua_newtable(L); // lektra.mousemap

    lua_pushlightuserdata(L, &config.mousebinds);

    // lektra.mousebind.set(button, modifiers, action)
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        auto *mousebinds = static_cast<std::vector<Config::MouseBinding> *>(
            lua_touserdata(L, lua_upvalueindex(1)));

        if (lua_gettop(L) != 3 || !lua_isinteger(L, 1) || !lua_isinteger(L, 2)
            || !lua_isinteger(L, 3))
        {
            return luaL_error(
                L,
                "Expected three integer arguments: button, modifiers, action");
        }

        const int button    = static_cast<int>(lua_tointeger(L, 1));
        const int modifiers = static_cast<int>(lua_tointeger(L, 2));
        const int action    = static_cast<int>(lua_tointeger(L, 3));

        mousebinds->push_back({static_cast<Qt::MouseButton>(button),
                               static_cast<Qt::KeyboardModifiers>(modifiers),
                               static_cast<GraphicsView::MouseAction>(action)});

        return 0; // No return values
    }, 1);

    lua_setfield(L, -2, "set");

    // lektra.mousebind.unset(button, modifiers)
    lua_pushlightuserdata(L, &config);
    lua_pushcclosure(L, [](lua_State *L) -> int
    {
        auto *mousebinds = static_cast<std::vector<Config::MouseBinding> *>(
            lua_touserdata(L, lua_upvalueindex(1)));

        if (lua_gettop(L) != 2 || !lua_isinteger(L, 1) || !lua_isinteger(L, 2))
        {
            return luaL_error(
                L, "Expected two integer arguments: button, modifiers");
        }

        const int button    = static_cast<int>(lua_tointeger(L, 1));
        const int modifiers = static_cast<int>(lua_tointeger(L, 2));

        auto it
            = std::remove_if(mousebinds->begin(), mousebinds->end(),
                             [button, modifiers](const Config::MouseBinding &mb)
        {
            return mb.button == static_cast<Qt::MouseButton>(button)
                   && mb.modifiers
                          == static_cast<Qt::KeyboardModifiers>(modifiers);
        });

        if (it == mousebinds->end())
        {
            return luaL_error(
                L, "Mouse binding not found for button %d with modifiers %d",
                button, modifiers);
        }
        else
        {
            mousebinds->erase(it, mousebinds->end());
        }

        return 0; // No return values
    }, 1);

    lua_setfield(L, -2, "unset");

    lua_setfield(L, -2, "mousemap");
}
} // namespace

void
Lektra::initLuaMousemaps() noexcept
{
    registerMousemaps(m_L, m_config);
}
