#include "Lektra.hpp"
#include "utils.hpp"

namespace
{

#define VIEW_METHOD(name, body)                                                \
    {name, [](lua_State *L) -> int                                             \
    {                                                                          \
        auto **view = static_cast<DocumentView **>(                            \
            luaL_checkudata(L, 1, "DocumentViewMetaTable"));                   \
        body                                                                   \
    }}

static const luaL_Reg DocumentViewMethods[] = {
    VIEW_METHOD("close",
                {
                    if (*view)
                        (*view)->close();
                    return 0;
                }),

    VIEW_METHOD("pageno",
                {
                    lua_pushinteger(L, (*view)->pageNo() + 1);
                    return 1;
                }),

    VIEW_METHOD("goto_page",
                {
                    if (*view)
                        (*view)->GotoPage(luaL_checkinteger(L, 2) - 1);
                    return 0;
                }),

    VIEW_METHOD("open",
                {
                    if (*view)
                    {
                        auto *lektra = static_cast<Lektra *>(
                            lua_touserdata(L, lua_upvalueindex(1)));
                        auto filename = luaL_checkstring(L, 2);
                        lektra->OpenFile(filename);
                    }
                    return 0;
                }),

    VIEW_METHOD("page_count",
                {
                    lua_pushinteger(L, (*view)->numPages());
                    return 1;
                }),

    VIEW_METHOD("goto_location",
                {
                    if (*view)
                    {
                        auto pageno
                            = static_cast<int>(luaL_checkinteger(L, 2) - 1);
                        auto x = static_cast<float>(luaL_checknumber(L, 3));
                        auto y = static_cast<float>(luaL_checknumber(L, 4));
                        (*view)->GotoLocation({pageno - 1, x, y});
                    }
                    return 0;
                }),

    VIEW_METHOD("location",
                {
                    if (*view)
                    {
                        auto loc = (*view)->CurrentLocation();
                        lua_pushinteger(L, loc.pageno + 1);
                        lua_pushnumber(L, loc.x);
                        lua_pushnumber(L, loc.y);
                        return 3;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("history_back",
                {
                    if (*view)
                        (*view)->GoBackHistory();
                    return 0;
                }),

    VIEW_METHOD("history_forward",
                {
                    if (*view)
                        (*view)->GoForwardHistory();
                    return 0;
                }),

    VIEW_METHOD("zoom",
                {
                    if (*view)
                    {
                        lua_pushnumber(L, (*view)->zoom());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                    return 0;
                }),

    VIEW_METHOD("set_zoom",
                {
                    if (*view)
                    {
                        auto factor
                            = static_cast<double>(luaL_checknumber(L, 2));
                        (*view)->setZoom(factor);
                    }
                    return 0;
                }),

    VIEW_METHOD("fit",
                {
                    if (*view)
                    {
                        auto fit_mode = (*view)->fitMode();
                        lua_pushinteger(L, static_cast<int>(fit_mode));
                    }
                    else
                    {
                        lua_pushnil(L);
                    }
                    return 0;
                }),

    VIEW_METHOD(
        "set_fit",
        {
            if (*view)
            {
                auto fit_mode = luaL_checkinteger(L, 2);
                switch (fit_mode)
                {
                    case static_cast<int>(DocumentView::FitMode::Width):
                        (*view)->setFitMode(DocumentView::FitMode::Width);
                        break;
                    case static_cast<int>(DocumentView::FitMode::Height):
                        (*view)->setFitMode(DocumentView::FitMode::Height);
                        break;
                    case static_cast<int>(DocumentView::FitMode::Window):
                        (*view)->setFitMode(DocumentView::FitMode::Window);
                        break;
                    default:
                        return luaL_error(L, "Invalid fit mode: %d", fit_mode);
                }
            }
            return 0;
        }),

    VIEW_METHOD("rotation",
                {
                    if (*view)
                    {
                        lua_pushnumber(L, (*view)->model()->rotation());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("set_rotation",
                {
                    if (*view)
                    {
                        auto rotation
                            = static_cast<double>(luaL_checknumber(L, 2));
                        (*view)->model()->setRotation(rotation);
                    }
                    return 0;
                }),

    VIEW_METHOD("layout",
                {
                    if (*view)
                    {
                        auto layout_mode = (*view)->layoutMode();
                        lua_pushinteger(L, static_cast<int>(layout_mode));
                    }
                    else
                    {
                        lua_pushnil(L);
                    }
                    return 0;
                }),

    VIEW_METHOD(
        "set_layout",
        {
            if (*view)
            {
                auto layout_mode = luaL_checkstring(L, 2);
                if (strcmp(layout_mode, "single") == 0)
                    (*view)->setLayoutMode(DocumentView::LayoutMode::SINGLE);
                else if (strcmp(layout_mode, "horizontal") == 0)
                    (*view)->setLayoutMode(
                        DocumentView::LayoutMode::HORIZONTAL);
                else if (strcmp(layout_mode, "vertical") == 0)
                    (*view)->setLayoutMode(DocumentView::LayoutMode::VERTICAL);
                else if (strcmp(layout_mode, "book") == 0)
                    (*view)->setLayoutMode(DocumentView::LayoutMode::BOOK);
                else
                    return luaL_error(L, "Invalid layout mode: %s",
                                      layout_mode);
            }
            return 0;
        }),

    VIEW_METHOD("has_selection",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->hasTextSelection());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("selection_text",
                {
                    if (*view)
                    {
                        bool formatted = lua_toboolean(L, 2);
                        auto text      = (*view)->selectionText(formatted);
                        lua_pushstring(L, text.toUtf8().constData());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("clear_selection",
                {
                    if (*view)
                        (*view)->ClearTextSelection();
                    return 0;
                }),

    VIEW_METHOD("search",
                {
                    if (*view)
                    {
                        auto term     = luaL_checkstring(L, 2);
                        auto useRegex = lua_toboolean(L, 3);
                        (*view)->Search(term, useRegex);
                    }
                    return 0;
                }),

    VIEW_METHOD("search_hit_next",
                {
                    if (*view)
                    {
                        (*view)->NextHit();
                    }
                    return 0;
                }),

    VIEW_METHOD("search_hit_prev",
                {
                    if (*view)
                    {
                        (*view)->PrevHit();
                    }
                    return 0;
                }),

    VIEW_METHOD("search_cancel",
                {
                    if (*view)
                        (*view)->SearchCancel();
                    return 0;
                }),

    VIEW_METHOD("search_hits",
                {
                    if (*view)
                    {
                    }

                    return 0;
                }),

    VIEW_METHOD("search_hit_count",
                {
                    if (*view)
                    {
                        lua_pushinteger(L,
                                        (*view)->model()->searchMatchesCount());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("file_path",
                {
                    if (*view)
                    {
                        auto path = (*view)->filePath();
                        lua_pushstring(L, path.toUtf8().constData());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),
    VIEW_METHOD("file_type",
                {
                    if (*view)
                    {
                        auto type = (*view)->model()->fileTypeToString();
                        lua_pushstring(L, type.toUtf8().constData());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("reload",
                {
                    if (*view)
                        (*view)->reloadFile();
                    return 0;
                }),

    VIEW_METHOD(
        "register",
        {
            if (*view)
            {
                const char *eventName = luaL_checkstring(L, 2);
                luaL_checktype(L, 3, LUA_TFUNCTION);

                lua_pushvalue(L, 3);
                // Store the callback in the registry with a unique key
                int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

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

                // view->addEventListener(DispatchType, CallbackFn)
                (*view)->addEventListener(dtype, callbackRef, false,
                                          [L, callbackRef](DocumentView *v)
                {
                    // Push the callback function onto the stack
                    lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRef);

                    // Push the view as an argument to the callback
                    auto **ud = static_cast<DocumentView **>(
                        lua_newuserdata(L, sizeof(DocumentView *)));
                    *ud = v;
                    luaL_getmetatable(L, "DocumentViewMetaTable");
                    lua_setmetatable(L, -2);

                    // Call the callback with 1 argument and no return values
                    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                    {
                        // Handle Lua errors (e.g., print the error message)
                        const char *errorMsg = lua_tostring(L, -1);
                        fprintf(stderr, "Lua callback error: %s\n", errorMsg);
                        lua_pop(L, 1); // Remove error message from stack
                    }
                });

                lua_pushinteger(L, callbackRef);
                return 1; // One return value (the handle)
            }

            return 0;
        }),

    VIEW_METHOD("unregister",
                {
                    if (*view)
                    {
                        const char *eventName = luaL_checkstring(L, 2);
                        int handle            = luaL_checkinteger(L, 3);

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

                        (*view)->removeEventListener(dtype, handle);
                    }
                    return 0;
                }),

    VIEW_METHOD("clear_listeners",
                {
                    if (*view)
                    {
                        const char *eventName = luaL_checkstring(L, 2);
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

                        (*view)->clearEventListeners(dtype);
                    }
                    return 0;
                }),

    VIEW_METHOD(
        "register_once",
        {
            if (*view)
            {
                // call the event only once
                const char *eventName = luaL_checkstring(L, 2);
                luaL_checktype(L, 3, LUA_TFUNCTION);

                // Store the callback in the registry with a unique key
                int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

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

                (*view)->addEventListener(
                    dtype, callbackRef, true,
                    [L, callbackRef, dtype](DocumentView *v)
                {
                    // Push the callback function onto the stack
                    lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRef);

                    // Push the view as an argument to the callback
                    auto **ud = static_cast<DocumentView **>(
                        lua_newuserdata(L, sizeof(DocumentView *)));
                    *ud = v;
                    luaL_getmetatable(L, "DocumentViewMetaTable");
                    lua_setmetatable(L, -2);

                    // Call the callback with 1 argument and no return values
                    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                    {
                        // Handle Lua errors (e.g., print the error message)
                        const char *errorMsg = lua_tostring(L, -1);
                        fprintf(stderr, "Lua callback error: %s\n", errorMsg);
                        lua_pop(L, 1); // Remove error message from stack
                    }

                    // Unregister this callback after it's called once
                    (*v).removeEventListener(dtype, callbackRef);
                });
            }
            return 0;
        }),

    VIEW_METHOD("is_modified",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->isModified());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("save",
                {
                    if (*view)
                        (*view)->SaveFile();
                    return 0;
                }),

    VIEW_METHOD("save_as",
                {
                    if (*view)
                    {
                    }
                    return 0;
                }),
    VIEW_METHOD("extract_text",
                {
                    if (*view)
                    {
                        bool formatted = lua_toboolean(L, 2);
                        auto text      = (*view)->extractText(formatted);
                        lua_pushstring(L, text.toUtf8().constData());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),
    {nullptr, nullptr}};

#undef VIEW_METHOD

static void
registerDocumentView(lua_State *L)
{
    // 1. Create the metatable
    luaL_newmetatable(L, "DocumentViewMetaTable");

    // 2. Set __index to itself
    // This trick means: "if a key isn't in the userdata, look in this
    // metatable"
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    // 3. Register the methods into the metatable
    luaL_setfuncs(L, DocumentViewMethods, 0);

    lua_pop(L, 1); // Pop the metatable
}
} // namespace

// Register the DocumentView* type with lua
void
Lektra::initLuaView() noexcept
{
    registerDocumentView(m_L);

    lua_newtable(m_L);

    // lektra.view.current() -> id
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        auto *currentView = lektra->currentDocument();
        if (currentView)
            lua_pushinteger(L, currentView->id());
        else
            lua_pushnil(L);
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "current");

    // lektra.view.get(id) -> DocumentView or nil
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        auto id    = static_cast<DocumentView::Id>(luaL_checkinteger(L, 1));
        auto *view = lektra->get_view_by_id(id);
        if (view)
        {
            auto **ud = static_cast<DocumentView **>(
                lua_newuserdata(L, sizeof(DocumentView *)));
            *ud = view;
            luaL_getmetatable(L, "DocumentViewMetaTable");
            lua_setmetatable(L, -2);
        }
        else
        {
            lua_pushnil(L);
        }
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "get");

    // lektra.view.list(tab_index) -> table of DocumentView
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        auto tab_id    = luaL_checkinteger(L, 1);
        auto container = qobject_cast<DocumentContainer *>(
            lektra->m_tab_widget->widget(tab_id));
        if (!container)
        {
            lua_pushnil(L);
            return 1;
        }
        auto views = container->getAllViews();
        lua_newtable(L);
        int index = 1;
        for (auto *view : views)
        {
            auto **ud = static_cast<DocumentView **>(
                lua_newuserdata(L, sizeof(DocumentView *)));
            *ud = view;
            luaL_getmetatable(L, "DocumentViewMetaTable");
            lua_setmetatable(L, -2);
            lua_rawseti(L, -2, index++);
        }
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "list");

    lua_setfield(m_L, -2, "view");
}
