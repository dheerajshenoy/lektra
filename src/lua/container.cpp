#include "DocumentContainer.hpp"
#include "Lektra.hpp"

namespace
{
#define CONTAINER_METHOD(name, body)                                           \
    {name, [](lua_State *L) -> int                                             \
    {                                                                          \
        auto **container = static_cast<DocumentContainer **>(                  \
            luaL_checkudata(L, 1, "ContainerMetaTable"));                      \
        if (!*container)                                                       \
            return luaL_error(L, "container is closed");                       \
        body                                                                   \
    }}

void
pushView(lua_State *L, DocumentView *view)
{
    if (!view)
    {
        lua_pushnil(L);
        return;
    }
    auto **ud = static_cast<DocumentView **>(
        lua_newuserdata(L, sizeof(DocumentView *)));
    *ud = view;
    luaL_getmetatable(L, "DocumentViewMetaTable");
    lua_setmetatable(L, -2);
}

// Splits the container's current view; `orientation` is the splitter's.
int
splitView(lua_State *L, Qt::Orientation orientation)
{
    auto **container = static_cast<DocumentContainer **>(
        luaL_checkudata(L, 1, "ContainerMetaTable"));
    if (!*container)
        return luaL_error(L, "container is closed");

    DocumentView *current = (*container)->view();
    if (!current)
    {
        lua_pushnil(L);
        return 1;
    }

    DocumentView *created
        = lua_isnoneornil(L, 2)
              ? (*container)->split(current, orientation)
              : (*container)->split(current, orientation,
                                    QString::fromUtf8(luaL_checkstring(L, 2)));
    if (auto *lektra = qobject_cast<Lektra *>((*container)->window()))
        lektra->syncTabSplits(*container);
    pushView(L, created);
    return 1;
}

const luaL_Reg ContainerMethods[] = {
    // container:vsplit([file]) — new view beside the current one (a vertical
    // divider), like the split_vertical command. Shows the same document
    // unless a file is given. Returns the new view.
    {"vsplit", [](lua_State *L) -> int
    { return splitView(L, Qt::Horizontal); }},

    // container:hsplit([file]) — new view below the current one.
    {"hsplit", [](lua_State *L) -> int { return splitView(L, Qt::Vertical); }},

    // container:views() -> View[]
    CONTAINER_METHOD("views",
                     {
                         const auto views = (*container)->getAllViews();
                         lua_createtable(L, static_cast<int>(views.size()), 0);
                         int i = 1;
                         for (DocumentView *view : views)
                         {
                             pushView(L, view);
                             lua_rawseti(L, -2, i++);
                         }
                         return 1;
                     }),

    CONTAINER_METHOD("view_count",
                     {
                         lua_pushinteger(L, (*container)->getViewCount());
                         return 1;
                     }),

    // container:view() -> the focused view
    CONTAINER_METHOD("view",
                     {
                         pushView(L, (*container)->view());
                         return 1;
                     }),

    // container:focus(view | "left"|"right"|"up"|"down")
    CONTAINER_METHOD(
        "focus",
        {
            if (lua_isstring(L, 2))
            {
                const QString dir = QString::fromUtf8(lua_tostring(L, 2));
                using D           = DocumentContainer::Direction;
                if (dir == "left")
                    (*container)->focusSplit(D::Left);
                else if (dir == "right")
                    (*container)->focusSplit(D::Right);
                else if (dir == "up")
                    (*container)->focusSplit(D::Up);
                else if (dir == "down")
                    (*container)->focusSplit(D::Down);
                else
                    return luaL_error(L, "direction must be \"left\", "
                                         "\"right\", \"up\" or \"down\"");
            }
            else
            {
                auto **view = static_cast<DocumentView **>(
                    luaL_checkudata(L, 2, "DocumentViewMetaTable"));
                if (*view)
                    (*container)->focusView(*view);
            }
            return 0;
        }),

    // container:close_view([view]) -> boolean. Closes a split (default: the
    // focused one). The last remaining view is never closed.
    CONTAINER_METHOD(
        "close_view",
        {
            DocumentView *target = (*container)->view();
            if (!lua_isnoneornil(L, 2))
            {
                auto **view = static_cast<DocumentView **>(
                    luaL_checkudata(L, 2, "DocumentViewMetaTable"));
                target = *view;
            }
            if (!target || (*container)->getViewCount() <= 1
                || !(*container)->getAllViews().contains(target))
            {
                lua_pushboolean(L, 0);
                return 1;
            }
            (*container)->closeView(target);
            if (auto *lektra = qobject_cast<Lektra *>((*container)->window()))
                lektra->syncTabSplits(*container);
            lua_pushboolean(L, 1);
            return 1;
        }),

    // container:close_others([view]) — closes every split except `view`
    // (default: the focused one).
    CONTAINER_METHOD(
        "close_others",
        {
            DocumentView *keep = (*container)->view();
            if (!lua_isnoneornil(L, 2))
            {
                auto **view = static_cast<DocumentView **>(
                    luaL_checkudata(L, 2, "DocumentViewMetaTable"));
                keep = *view;
            }
            if (keep && (*container)->getAllViews().contains(keep))
            {
                (*container)->close_other_views(keep);
                if (auto *lektra
                    = qobject_cast<Lektra *>((*container)->window()))
                    lektra->syncTabSplits(*container);
            }
            return 0;
        }),

    CONTAINER_METHOD("toggle_maximize",
                     {
                         (*container)->toggleMaximizeSplit();
                         return 0;
                     }),

    CONTAINER_METHOD("is_maximized",
                     {
                         lua_pushboolean(L, (*container)->isMaximized());
                         return 1;
                     }),

    {nullptr, nullptr}};
} // namespace

void
Lektra::initLuaContainer() noexcept
{
    luaL_newmetatable(m_L, "ContainerMetaTable");
    lua_pushvalue(m_L, -1);
    lua_setfield(m_L, -2, "__index");
    luaL_setfuncs(m_L, ContainerMethods, 0);
    lua_pop(m_L, 1);
}
