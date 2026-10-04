#include "Lektra.hpp"
#include "PageRange.hpp"
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

const char *
linkTypeName(BrowseLinkItem::LinkType t)
{
    switch (t)
    {
        case BrowseLinkItem::LinkType::Page:
            return "page";
        case BrowseLinkItem::LinkType::Section:
            return "section";
        case BrowseLinkItem::LinkType::FitV:
            return "fit_v";
        case BrowseLinkItem::LinkType::FitH:
            return "fit_h";
        case BrowseLinkItem::LinkType::Location:
            return "location";
        case BrowseLinkItem::LinkType::External:
            return "external";
    }
    return "external";
}

// Reads a page location (1-based page, page-space x/y in points) from the
// table at `idx`: {page=, x=, y=} or {page, x, y}.
static bool
readLocation(lua_State *L, int idx, int *pageno, float *x, float *y)
{
    if (!lua_istable(L, idx))
        return false;
    auto field = [&](const char *name, int pos) -> lua_Number
    {
        lua_getfield(L, idx, name);
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_rawgeti(L, idx, pos);
        }
        const lua_Number v = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return v;
    };
    *pageno = static_cast<int>(field("page", 1)) - 1;
    *x      = static_cast<float>(field("x", 2));
    *y      = static_cast<float>(field("y", 3));
    return true;
}

static void
pushLink(lua_State *L, const Model::PageLink &link, int pageno, int index)
{
    lua_newtable(L);
    lua_pushinteger(L, pageno + 1);
    lua_setfield(L, -2, "page");
    lua_pushinteger(L, index + 1);
    lua_setfield(L, -2, "index");

    lua_newtable(L);
    lua_pushnumber(L, link.rect.left());
    lua_setfield(L, -2, "x0");
    lua_pushnumber(L, link.rect.top());
    lua_setfield(L, -2, "y0");
    lua_pushnumber(L, link.rect.right());
    lua_setfield(L, -2, "x1");
    lua_pushnumber(L, link.rect.bottom());
    lua_setfield(L, -2, "y1");
    lua_setfield(L, -2, "rect");

    lua_pushstring(L, link.info.uri.toUtf8().constData());
    lua_setfield(L, -2, "uri");
    lua_pushstring(L, linkTypeName(link.info.type));
    lua_setfield(L, -2, "type");

    if (link.info.target_page >= 0)
    {
        lua_pushinteger(L, link.info.target_page + 1);
        lua_setfield(L, -2, "target_page");
        if (!std::isnan(link.info.target_loc.x))
        {
            lua_pushnumber(L, link.info.target_loc.x);
            lua_setfield(L, -2, "target_x");
        }
        if (!std::isnan(link.info.target_loc.y))
        {
            lua_pushnumber(L, link.info.target_loc.y);
            lua_setfield(L, -2, "target_y");
        }
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

    VIEW_METHOD("page_text",
                {
                    // page_text([pageno], [formatted]): the text of a page
                    // (default: the current one), "" if it has none.
                    if (!*view)
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                    const int pageno
                        = lua_isnoneornil(L, 2)
                              ? (*view)->pageNo()
                              : static_cast<int>(luaL_checkinteger(L, 2) - 1);
                    if (pageno < 0 || pageno >= (*view)->numPages())
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                    const bool formatted = lua_toboolean(L, 3);
                    const QString text = (*view)->pageText(pageno, formatted);
                    lua_pushstring(L, text.toUtf8().constData());
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

    // --- links ---------------------------------------------------------
    VIEW_METHOD("links",
                {
                    if (!*view)
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                    const int pageno = lua_isnoneornil(L, 2)
                                           ? (*view)->pageNo()
                                           : static_cast<int>(
                                                 luaL_checkinteger(L, 2) - 1);
                    const auto links = (*view)->model()->pageLinks(pageno);
                    lua_createtable(L, static_cast<int>(links.size()), 0);
                    for (size_t i = 0; i < links.size(); ++i)
                    {
                        pushLink(L, links[i], pageno, static_cast<int>(i));
                        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
                    }
                    return 1;
                }),

    VIEW_METHOD("follow_link",
                {
                    if (!*view)
                        return 0;
                    // A link table from view:links(), or (page, index).
                    int pageno = 0;
                    int index  = 0;
                    if (lua_istable(L, 2))
                    {
                        lua_getfield(L, 2, "page");
                        pageno = static_cast<int>(lua_tointeger(L, -1)) - 1;
                        lua_getfield(L, 2, "index");
                        index = static_cast<int>(lua_tointeger(L, -1)) - 1;
                        lua_pop(L, 2);
                    }
                    else
                    {
                        pageno = static_cast<int>(luaL_checkinteger(L, 2)) - 1;
                        index  = static_cast<int>(luaL_checkinteger(L, 3)) - 1;
                    }
                    const auto links = (*view)->model()->pageLinks(pageno);
                    if (index < 0 || index >= static_cast<int>(links.size()))
                    {
                        lua_pushboolean(L, 0);
                        return 1;
                    }
                    (*view)->FollowLink(links[index].info);
                    lua_pushboolean(L, 1);
                    return 1;
                }),

    VIEW_METHOD("link_hints",
                {
                    // Starts hint mode in the current tab: "visit" (default)
                    // follows the chosen link, "copy" copies its address.
                    auto *lektra = qobject_cast<Lektra *>((*view)->window());
                    if (!lektra || lektra->currentDocument() != *view)
                    {
                        lua_pushboolean(L, 0);
                        return 1;
                    }
                    const QString mode = lua_isnoneornil(L, 2)
                                             ? QStringLiteral("visit")
                                             : QString::fromUtf8(
                                                   luaL_checkstring(L, 2));
                    if (mode == "copy")
                        lektra->CopyLinkKB();
                    else if (mode == "visit")
                        lektra->VisitLinkKB();
                    else
                        return luaL_error(L, "mode must be \"visit\" or "
                                             "\"copy\"");
                    lua_pushboolean(L, 1);
                    return 1;
                }),

    // --- text selection ------------------------------------------------
    VIEW_METHOD("select_range",
                {
                    if (!*view)
                        return 0;
                    PageLocation a{};
                    PageLocation b{};
                    int pa = 0;
                    int pb = 0;
                    if (lua_istable(L, 2) && lua_istable(L, 3))
                    {
                        readLocation(L, 2, &pa, &a.x, &a.y);
                        readLocation(L, 3, &pb, &b.x, &b.y);
                    }
                    else
                    {
                        pa  = static_cast<int>(luaL_checkinteger(L, 2)) - 1;
                        a.x = static_cast<float>(luaL_checknumber(L, 3));
                        a.y = static_cast<float>(luaL_checknumber(L, 4));
                        pb  = static_cast<int>(luaL_checkinteger(L, 5)) - 1;
                        b.x = static_cast<float>(luaL_checknumber(L, 6));
                        b.y = static_cast<float>(luaL_checknumber(L, 7));
                    }
                    a.pageno = pa;
                    b.pageno = pb;
                    lua_pushboolean(L, (*view)->SelectTextRange(a, b));
                    return 1;
                }),

    VIEW_METHOD("select_region",
                {
                    if (!*view)
                        return 0;
                    const int pageno
                        = static_cast<int>(luaL_checkinteger(L, 2)) - 1;
                    const auto x0 = static_cast<float>(luaL_checknumber(L, 3));
                    const auto y0 = static_cast<float>(luaL_checknumber(L, 4));
                    const auto x1 = static_cast<float>(luaL_checknumber(L, 5));
                    const auto y1 = static_cast<float>(luaL_checknumber(L, 6));
                    PageLocation from{};
                    PageLocation to{};
                    from.pageno = to.pageno = pageno;
                    from.x                  = std::min(x0, x1);
                    from.y                  = std::min(y0, y1);
                    to.x                    = std::max(x0, x1);
                    to.y                    = std::max(y0, y1);
                    lua_pushboolean(L, (*view)->SelectTextRange(from, to));
                    return 1;
                }),

    // --- scrolling and geometry ---------------------------------------
    VIEW_METHOD("scroll",
                {
                    if (*view)
                        (*view)->ScrollBy(
                            static_cast<int>(luaL_optinteger(L, 2, 0)),
                            static_cast<int>(luaL_optinteger(L, 3, 0)));
                    return 0;
                }),

    VIEW_METHOD("export_pages",
                {
                    // export_pages(names, [pages], [{dpi=, overwrite=}])
                    //   -> {paths...} | nil, error
                    auto failure = [L](const char *message)
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, message);
                        return 2;
                    };
                    if (!*view)
                        return failure("the view is closed");
                    DocumentView *doc = *view;

                    // the file name(s): one string, or a list of strings
                    QStringList names;
                    if (lua_istable(L, 2))
                    {
                        const int n = static_cast<int>(lua_rawlen(L, 2));
                        for (int i = 1; i <= n; ++i)
                        {
                            lua_rawgeti(L, 2, i);
                            if (lua_isstring(L, -1))
                                names << QString::fromUtf8(lua_tostring(L, -1));
                            lua_pop(L, 1);
                        }
                    }
                    else
                        names << QString::fromUtf8(luaL_checkstring(L, 2));

                    // the pages: nothing (the current one), a number, a text
                    // like "1-5,8" (or "all"), or a list of numbers; 1-based
                    const int count = doc->model() ? doc->model()->numPages() : 0;
                    QString problem;
                    std::vector<int> pages;
                    if (lua_isnoneornil(L, 3))
                        pages = {doc->pageNo()};
                    else if (lua_type(L, 3) == LUA_TNUMBER)
                    {
                        const int p = static_cast<int>(lua_tointeger(L, 3));
                        if (p < 1 || p > count)
                            return failure(
                                qUtf8Printable(QStringLiteral("page %1 does not exist (the pages are 1 to %2)")
                                                   .arg(p)
                                                   .arg(count)));
                        pages = {p - 1};
                    }
                    else if (lua_type(L, 3) == LUA_TSTRING)
                    {
                        pages = page_range::parse(QString::fromUtf8(lua_tostring(L, 3)),
                                                  count, doc->pageNo(), &problem);
                        if (pages.empty())
                            return failure(qUtf8Printable(problem));
                    }
                    else if (lua_istable(L, 3))
                    {
                        const int n = static_cast<int>(lua_rawlen(L, 3));
                        for (int i = 1; i <= n; ++i)
                        {
                            lua_rawgeti(L, 3, i);
                            const int p = static_cast<int>(lua_tointeger(L, -1));
                            lua_pop(L, 1);
                            if (p < 1 || p > count)
                                return failure(
                                    qUtf8Printable(QStringLiteral("page %1 does not exist (the pages are 1 to %2)")
                                                       .arg(p)
                                                       .arg(count)));
                            pages.push_back(p - 1);
                        }
                        if (pages.empty())
                            return failure("no pages were given");
                    }
                    else
                        return failure("pages is a number, a text like \"1-5,8\", or a list of numbers");

                    int dpi        = 150;
                    bool overwrite = false;
                    bool split     = false;
                    if (lua_istable(L, 4))
                    {
                        lua_getfield(L, 4, "dpi");
                        if (lua_isnumber(L, -1))
                            dpi = static_cast<int>(lua_tointeger(L, -1));
                        lua_pop(L, 1);
                        lua_getfield(L, 4, "overwrite");
                        overwrite = lua_toboolean(L, -1);
                        lua_pop(L, 1);
                        lua_getfield(L, 4, "split");
                        split = lua_toboolean(L, -1);
                        lua_pop(L, 1);
                    }

                    QStringList written;
                    QString error;
                    if (!doc->exportPages(names, pages, dpi, overwrite, &written, &error,
                                          nullptr, split))
                        return failure(qUtf8Printable(error));
                    lua_newtable(L);
                    for (int i = 0; i < written.size(); ++i)
                    {
                        lua_pushstring(L, written.at(i).toUtf8().constData());
                        lua_rawseti(L, -2, i + 1);
                    }
                    return 1;
                }),

    VIEW_METHOD("scroll_to",
                {
                    if (*view)
                    {
                        const QPoint cur = (*view)->scrollPosition();
                        (*view)->ScrollTo(
                            static_cast<int>(luaL_optinteger(L, 2, cur.x())),
                            static_cast<int>(luaL_optinteger(L, 3, cur.y())));
                    }
                    return 0;
                }),

    VIEW_METHOD("scroll_position",
                {
                    if (!*view)
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                    const QPoint pos = (*view)->scrollPosition();
                    const QPoint max = (*view)->scrollMaximum();
                    lua_pushinteger(L, pos.x());
                    lua_pushinteger(L, pos.y());
                    lua_pushinteger(L, max.x());
                    lua_pushinteger(L, max.y());
                    return 4;
                }),

    VIEW_METHOD("visible_pages",
                {
                    if (!*view)
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                    const auto pages = (*view)->VisiblePages();
                    lua_createtable(L, static_cast<int>(pages.size()), 0);
                    for (size_t i = 0; i < pages.size(); ++i)
                    {
                        lua_pushinteger(L, pages[i] + 1);
                        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
                    }
                    return 1;
                }),

    VIEW_METHOD("page_size",
                {
                    if (!*view)
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                    const int pageno
                        = lua_isnoneornil(L, 2)
                              ? (*view)->pageNo()
                              : static_cast<int>(luaL_checkinteger(L, 2) - 1);
                    if (pageno < 0 || pageno >= (*view)->numPages())
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                    const QSizeF dim
                        = (*view)->model()->pageSizePts(pageno, true);
                    lua_pushnumber(L, dim.width());
                    lua_pushnumber(L, dim.height());
                    return 2;
                }),

    VIEW_METHOD("page_sizes",
                {
                    // {{width=, height=, known=}, ...}. Sizes of pages that
                    // were never loaded are the document default (known =
                    // false); use page_size() for an exact value.
                    if (!*view)
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                    Model *model    = (*view)->model();
                    const int count = (*view)->numPages();
                    const int first = std::max<int>(
                        1, static_cast<int>(luaL_optinteger(L, 2, 1)));
                    const int last = std::min<int>(
                        count, static_cast<int>(luaL_optinteger(L, 3, count)));
                    lua_createtable(L, std::max(0, last - first + 1), 0);
                    int n = 0;
                    for (int p = first; p <= last; ++p)
                    {
                        bool known      = false;
                        const QSizeF dim = model->pageSizePts(p - 1, false, &known);
                        lua_createtable(L, 0, 3);
                        lua_pushnumber(L, dim.width());
                        lua_setfield(L, -2, "width");
                        lua_pushnumber(L, dim.height());
                        lua_setfield(L, -2, "height");
                        lua_pushboolean(L, known);
                        lua_setfield(L, -2, "known");
                        lua_rawseti(L, -2, ++n);
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
