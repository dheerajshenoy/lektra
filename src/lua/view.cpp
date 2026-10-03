#include "Lektra.hpp"
#include "Model.hpp"
#include "utils.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QMenu>
#include <cstring>

namespace
{

static void
push_outline_nodes(lua_State *L, fz_outline *node, Model *model)
{
    lua_newtable(L);
    int idx = 1;
    for (fz_outline *n = node; n; n = n->next, ++idx)
    {
        lua_newtable(L);

        lua_pushstring(L, n->title ? n->title : "");
        lua_setfield(L, -2, "title");

        // Resolve via the Model rather than reading n->page.page
        // directly: for chaptered formats (EPUB) that number is only
        // the local page within n->page.chapter, and EPUB additionally
        // leaves n->page/x/y unresolved (sentinel {-1,-1}), requiring
        // resolution from n->uri.
        float     x = n->x, y = n->y;
        const int pageno = model ? model->resolveOutlineNode(n, &x, &y)
                                 : n->page.page;
        if (pageno >= 0)
            lua_pushinteger(L, pageno + 1);
        else
            lua_pushnil(L);
        lua_setfield(L, -2, "pageno");

        lua_pushnumber(L, x);
        lua_setfield(L, -2, "x");

        lua_pushnumber(L, y);
        lua_setfield(L, -2, "y");

        push_outline_nodes(L, n->down,
                           model); // empty table when n->down == nullptr
        lua_setfield(L, -2, "children");

        lua_rawseti(L, -2, idx);
    }
}

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

    VIEW_METHOD("undo",
                {
                    if (*view)
                        (*view)->Undo();
                    return 0;
                }),

    VIEW_METHOD("redo",
                {
                    if (*view)
                        (*view)->Redo();
                    return 0;
                }),

    VIEW_METHOD("properties",
                {
                    if (auto model = (*view)->model())
                    {
                        auto props = model->properties();
                        lua_newtable(L);
                        for (const auto &[key, value] : props)
                        {
                            std::string keyStr   = key.toUtf8().toStdString();
                            std::string valueStr = value.toUtf8().toStdString();

                            lua_pushstring(L, keyStr.c_str());
                            lua_pushstring(L, valueStr.c_str());
                            lua_settable(L, -3);
                        }

                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                    }

                    return 1;
                }),

    VIEW_METHOD("set_dpr",
                {
                    if (*view)
                        (*view)->setDPR(
                            static_cast<float>(luaL_checknumber(L, 2)));
                    return 0;
                }),

    VIEW_METHOD("dpr",
                {
                    if (*view)
                    {
                        lua_pushnumber(L, (*view)->dpr());
                    }
                    else
                    {
                        lua_pushnil(L);
                    }

                    return 1;
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
                        auto *lektra
                            = qobject_cast<Lektra *>((*view)->window());
                        if (lektra)
                            lektra->OpenFile(luaL_checkstring(L, 2));
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
                        (*view)->GotoLocation({pageno, x, y});
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
                        lua_pushnumber(L, (*view)->zoom());
                    else
                        lua_pushnil(L);
                    return 1;
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

    VIEW_METHOD("fit",
                {
                    if (*view)
                        lua_pushinteger(L,
                                        static_cast<int>((*view)->fitMode()));
                    else
                        lua_pushnil(L);
                    return 1;
                }),

    VIEW_METHOD("model",
                {
                    if (*view && (*view)->model())
                    {
                        auto **ud = static_cast<Model **>(
                            lua_newuserdata(L, sizeof(Model *)));
                        *ud = (*view)->model();
                        luaL_getmetatable(L, "ModelMetaTable");
                        lua_setmetatable(L, -2);
                    }
                    else
                    {
                        lua_pushnil(L);
                    }

                    return 1;
                }),

    // auto **ud = static_cast<DocumentView **>(
    //     lua_newuserdata(L, sizeof(DocumentView *)));
    // *ud = lektra->currentDocument();
    // luaL_getmetatable(L, "DocumentViewMetaTable");
    // lua_setmetatable(L, -2);

    VIEW_METHOD("mode",
                {
                    if (*view)
                        lua_pushinteger(
                            L, static_cast<int>((*view)->selectionMode()));
                    else
                        lua_pushnil(L);
                    return 1;
                }),

    VIEW_METHOD("set_invert",
                {
                    if (*view)
                        (*view)->setInvertColor(lua_toboolean(L, 2));
                    return 0;
                }),

    VIEW_METHOD("is_modified",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->isModified());
                    }
                    else
                    {
                        lua_pushnil(L);
                    }

                    return 1;
                }),

    VIEW_METHOD("is_invert",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->invertColor());
                    }
                    else
                    {
                        lua_pushnil(L);
                    }

                    return 1;
                }),

    VIEW_METHOD("set_mode",
                { return luaL_error(L, "set_mode: not yet implemented"); }),

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
                        lua_pushinteger(
                            L, static_cast<int>((*view)->layoutMode()));
                    else
                        lua_pushnil(L);
                    return 1;
                }),

    VIEW_METHOD(
        "set_layout",
        {
            if (*view)
            {
                auto layout_mode = luaL_checkinteger(L, 2);
                switch (layout_mode)
                {
                    case static_cast<int>(DocumentView::LayoutMode::SINGLE):
                        (*view)->setLayoutMode(
                            DocumentView::LayoutMode::SINGLE);
                        break;
                    case static_cast<int>(DocumentView::LayoutMode::HORIZONTAL):
                        (*view)->setLayoutMode(
                            DocumentView::LayoutMode::HORIZONTAL);
                        break;
                    case static_cast<int>(DocumentView::LayoutMode::VERTICAL):
                        (*view)->setLayoutMode(
                            DocumentView::LayoutMode::VERTICAL);
                        break;
                    case static_cast<int>(DocumentView::LayoutMode::BOOK):
                        (*view)->setLayoutMode(DocumentView::LayoutMode::BOOK);
                        break;
                    default:
                        return luaL_error(L, "Invalid layout mode: %d",
                                          layout_mode);
                }
            }

            return 0;
        }),

    VIEW_METHOD("is_portal",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->is_portal());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("set_portal",
                {
                    if (*view)
                    {
                        auto *portal
                            = static_cast<DocumentView *>(lua_touserdata(L, 2));
                        if (portal)
                            (*view)->setPortal(portal);
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("set_active",
                {
                    if (*view)
                    {
                        bool active = lua_toboolean(L, 2);
                        (*view)->setActive(active);
                    }
                    return 0;
                }),

    VIEW_METHOD("is_active",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->isActive());
                    }
                    else
                    {
                        lua_pushnil(L);
                    }

                    return 1;
                }),

    VIEW_METHOD("is_visual_line_mode",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->visual_line_mode());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("set_visual_line_mode",
                {
                    if (*view)
                    {
                        bool enabled = lua_toboolean(L, 2);
                        (*view)->set_visual_line_mode(enabled);
                    }
                    return 0;
                }),

    VIEW_METHOD("is_thumbnail_view",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->isThumbnailView());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
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

    VIEW_METHOD("search_below",
                {
                    if (*view)
                    {
                        auto term     = luaL_checkstring(L, 2);
                        auto useRegex = lua_toboolean(L, 3);
                        (*view)->setSearchScope(
                            DocumentView::SearchScope::Below);
                        (*view)->Search(term, useRegex);
                    }
                    return 0;
                }),

    VIEW_METHOD("search_above",
                {
                    if (*view)
                    {
                        auto term     = luaL_checkstring(L, 2);
                        auto useRegex = lua_toboolean(L, 3);
                        (*view)->setSearchScope(
                            DocumentView::SearchScope::Above);
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
                DispatchType type
                    = static_cast<DispatchType>(luaL_checkinteger(L, 2));

                luaL_checktype(L, 3, LUA_TFUNCTION);
                lua_pushvalue(L, 3);

                // Store the callback in the registry with a unique key
                int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

                // view->addEventListener(DispatchType, CallbackFn)
                (*view)->addEventListener(type, callbackRef, false,
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

                    // Call the callback with 1 argument and no return
                    // values
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
        "register_context_menu",
        {
            if (*view)
            {
                const char *eventName = luaL_checkstring(L, 2);
                luaL_checktype(L, 3, LUA_TFUNCTION);

                lua_pushvalue(L, 3);
                int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

                DocumentView::ContextMenuType menuType;
                if (strcmp(eventName, "TextSelection") == 0)
                    menuType = DocumentView::ContextMenuType::TextSelection;
                else if (strcmp(eventName, "RegionSelection") == 0)
                    menuType = DocumentView::ContextMenuType::RegionSelection;
                else
                    return luaL_error(L, "Unknown context menu type: %s",
                                      eventName);

                (*view)->addContextMenuListener(
                    menuType, callbackRef, false,
                    [L, callbackRef](DocumentView *v, QMenu *menu)
                {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRef);
                    auto **ud = static_cast<DocumentView **>(
                        lua_newuserdata(L, sizeof(DocumentView *)));
                    *ud = v;
                    luaL_getmetatable(L, "DocumentViewMetaTable");
                    lua_setmetatable(L, -2);

                    QMenu **menu_ud = static_cast<QMenu **>(
                        lua_newuserdata(L, sizeof(QMenu *)));
                    *menu_ud = menu;
                    luaL_setmetatable(L, "LektraMenu");

                    if (lua_pcall(L, 2, 0, 0) != LUA_OK)
                    {
                        const char *errorMsg = lua_tostring(L, -1);
                        fprintf(stderr, "Lua context menu callback error: %s\n",
                                errorMsg);
                        lua_pop(L, 1);
                    }
                });

                lua_pushinteger(L, callbackRef);
                return 1;
            }

            return 0;
        }),

    VIEW_METHOD(
        "unregister_context_menu",
        {
            if (*view)
            {
                const char *eventName = luaL_checkstring(L, 2);
                int handle            = luaL_checkinteger(L, 3);

                DocumentView::ContextMenuType menuType;
                if (strcmp(eventName, "TextSelection") == 0)
                    menuType = DocumentView::ContextMenuType::TextSelection;
                else if (strcmp(eventName, "RegionSelection") == 0)
                    menuType = DocumentView::ContextMenuType::RegionSelection;
                else
                    return luaL_error(L, "Unknown context menu type: %s",
                                      eventName);

                (*view)->removeContextMenuListener(menuType, handle);
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

                    // Call the callback with 1 argument and no return
                    // values
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

    VIEW_METHOD("is_image",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->model()->isImage());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("id",
                {
                    if (*view)
                    {
                        lua_pushinteger(L, (*view)->id());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("spacing",
                {
                    if (*view)
                    {
                        lua_pushnumber(L, (*view)->spacing());
                        return 1;
                    }
                    else
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                }),

    VIEW_METHOD("set_spacing",
                {
                    if (*view)
                    {
                        int spacing = lua_tonumber(L, 2);
                        if (spacing > 0)
                            (*view)->setSpacing(spacing);
                    }

                    return 0;
                }),

    VIEW_METHOD("auto_reload",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->autoReload());
                    }
                    else
                    {
                        lua_pushnil(L);
                    }

                    return 1;
                }),

    VIEW_METHOD("visual_line_mode",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->visual_line_mode());
                    }
                    else
                    {
                        lua_pushnil(L);
                    }

                    return 1;
                }),

    VIEW_METHOD("set_visual_line_mode",
                {
                    if (*view)
                    {
                        bool mode = lua_toboolean(L, 2);
                        (*view)->set_visual_line_mode(mode);
                    }

                    return 0;
                }),

    VIEW_METHOD("set_auto_reload",
                {
                    if (*view)
                    {
                        bool auto_reload = lua_toboolean(L, 2);
                        (*view)->setAutoReload(auto_reload);
                    }

                    return 0;
                }),

    VIEW_METHOD("save",
                {
                    if (*view)
                        (*view)->SaveFile();
                    return 0;
                }),

    VIEW_METHOD("save_as",
                { return luaL_error(L, "save_as: not yet implemented"); }),

    VIEW_METHOD("extract_text",
                {
                    if (*view)
                    {
                        bool formatted = lua_toboolean(L, 2);
                        auto text      = (*view)->extractText(formatted);
                        lua_pushstring(L, text.toUtf8().constData());
                    }
                    else
                    {
                        lua_pushnil(L);
                    }

                    return 1;
                }),

    VIEW_METHOD("container",
                {
                    if (*view)
                    {
                        auto *container = (*view)->container();
                        if (container)
                        {
                            auto **ud = static_cast<DocumentContainer **>(
                                lua_newuserdata(L,
                                                sizeof(DocumentContainer *)));
                            *ud = container;
                            luaL_getmetatable(L, "ContainerMetaTable");
                            lua_setmetatable(L, -2);
                        }
                    }
                    else
                    {
                        lua_pushnil(L);
                    }

                    return 1;
                }),

    VIEW_METHOD("outline",
                {
                    if (*view)
                    {
                        fz_outline *outline = (*view)->model()->getOutline();
                        push_outline_nodes(L, outline, (*view)->model());
                    }
                    else
                    {
                        lua_pushnil(L);
                    }

                    return 1;
                }),

    VIEW_METHOD("export_highlights",
                {
                    if (!*view)
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, "no active view");
                        return 2;
                    }

                    const char *path = luaL_checkstring(L, 2);
                    const bool ok    = (*view)->model()->exportTextHighlights(
                        QString::fromUtf8(path));

                    if (!ok)
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, "failed to write file");
                        return 2;
                    }

                    lua_pushboolean(L, 1);
                    return 1;
                }),

    VIEW_METHOD("region_select",
                {
                    luaL_checktype(L, 2, LUA_TFUNCTION);
                    lua_pushvalue(L, 2);
                    int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

                    (*view)->startRegionSelect(
                        [L, cb_ref](QRectF area)
                    {
                        lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);
                        luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);

                        lua_newtable(L);
                        lua_pushnumber(L, area.x());
                        lua_setfield(L, -2, "x");
                        lua_pushnumber(L, area.y());
                        lua_setfield(L, -2, "y");
                        lua_pushnumber(L, area.width());
                        lua_setfield(L, -2, "w");
                        lua_pushnumber(L, area.height());
                        lua_setfield(L, -2, "h");

                        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                        {
                            fprintf(stderr, "Lua error in region_select callback: %s\n",
                                    lua_tostring(L, -1));
                            lua_pop(L, 1);
                        }
                    });
                    return 0;
                }),

    // Same interaction as region_select, but instead of the selected
    // rect, the callback is passed the selected region rendered as a
    // base64-encoded PNG string (empty string if the region didn't map
    // onto a rendered page) — handy for feeding a screenshot region to an
    // OCR/vision-model API or saving it out via io.open + a base64 decoder.
    // view:opt() -> this view's local options. Writes affect only this view;
    // lektra.opt sets the global default and the current view.
    VIEW_METHOD("opt",
                {
                    auto *lektra = *view ? qobject_cast<Lektra *>((*view)->window())
                                         : nullptr;
                    if (!lektra)
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                    lektra->pushViewOptTable(L, *view);
                    return 1;
                }),

    VIEW_METHOD("region_select_image",
                {
                    luaL_checktype(L, 2, LUA_TFUNCTION);
                    lua_pushvalue(L, 2);
                    int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

                    DocumentView *self = *view;
                    (*view)->startRegionSelect(
                        [L, cb_ref, self](QRectF area)
                    {
                        lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);
                        luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);

                        QByteArray bytes;
                        if (self)
                        {
                            const QImage img = self->regionImage(area);
                            if (!img.isNull())
                            {
                                QBuffer buf(&bytes);
                                buf.open(QIODevice::WriteOnly);
                                img.save(&buf, "PNG");
                            }
                        }
                        const QByteArray b64 = bytes.toBase64();
                        lua_pushlstring(L, b64.constData(), b64.size());

                        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                        {
                            fprintf(stderr,
                                    "Lua error in region_select_image "
                                    "callback: %s\n",
                                    lua_tostring(L, -1));
                            lua_pop(L, 1);
                        }
                    });
                    return 0;
                }),

    VIEW_METHOD("narrow_to_region",
                {
                    if (*view)
                        (*view)->NarrowToRegion();
                    return 0;
                }),

    VIEW_METHOD("narrow_to_pages",
                {
                    if (*view)
                    {
                        auto start = luaL_checkinteger(L, 2);
                        auto end   = luaL_checkinteger(L, 3);
                        (*view)->NarrowToPages(static_cast<int>(start),
                                               static_cast<int>(end));
                    }
                    return 0;
                }),

    VIEW_METHOD("narrow_to_section",
                {
                    if (*view)
                    {
                        auto title = luaL_checkstring(L, 2);
                        (*view)->NarrowToSectionByTitle(
                            QString::fromUtf8(title));
                    }
                    return 0;
                }),

    VIEW_METHOD("widen_region",
                {
                    if (*view)
                        (*view)->WidenRegion();
                    return 0;
                }),

    VIEW_METHOD("is_narrowed",
                {
                    if (*view)
                    {
                        lua_pushboolean(L, (*view)->isNarrowed());
                        return 1;
                    }
                    lua_pushnil(L);
                    return 1;
                }),

    VIEW_METHOD("rotate_clock",
                {
                    if (*view)
                        (*view)->RotateClock();
                    return 0;
                }),

    VIEW_METHOD("rotate_anticlock",
                {
                    if (*view)
                        (*view)->RotateAnticlock();
                    return 0;
                }),

    VIEW_METHOD("flip_horizontal",
                {
                    if (*view)
                        (*view)->FlipH();
                    return 0;
                }),

    VIEW_METHOD("flip_vertical",
                {
                    if (*view)
                        (*view)->FlipV();
                    return 0;
                }),

    {nullptr, nullptr}}; // end point

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

    // lektra.view.current() -> View
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        auto *currentView = lektra->currentDocument();
        if (currentView)
        {
            auto **ud = static_cast<DocumentView **>(
                lua_newuserdata(L, sizeof(DocumentView *)));
            *ud = lektra->currentDocument();
            luaL_getmetatable(L, "DocumentViewMetaTable");
            lua_setmetatable(L, -2);
        }
        else
            lua_pushnil(L);
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "current");

    // lektra.view.get(id) -> View or nil
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

    // lektra.view.list(tab_index) -> table of View
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
