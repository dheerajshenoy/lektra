#include "DocumentContainer.hpp"
#include "Lektra.hpp"
#include "utils.hpp"

namespace
{
struct TabHandle
{
    TabWidget *widget = nullptr;
    int index         = -1;
};

#define TAB_METHOD(name, body)                                                 \
    {name, [](lua_State *L) -> int                                             \
    {                                                                          \
        auto *tab                                                              \
            = static_cast<TabHandle *>(luaL_checkudata(L, 1, "TabMetaTable")); \
        body                                                                   \
    }}

static const luaL_Reg TabMethods[]
    = {TAB_METHOD("id",
                  {
                      if (!tab || !tab->widget)
                      {
                          lua_pushnil(L);
                          return 1;
                      }

                      if (tab->index < 0 || tab->index >= tab->widget->count())
                      {
                          lua_pushnil(L);
                          return 1;
                      }

                      int id = tab->widget->id(tab->index);
                      if (id == -1)
                      {
                          lua_pushnil(L);
                      }
                      else
                      {
                          lua_pushinteger(L, id);
                      }

                      return 1;
                  }),
       {"close",
        [](lua_State *L) -> int
{
    auto *tab = static_cast<TabHandle *>(luaL_checkudata(L, 1, "TabMetaTable"));
    if (tab && tab->widget && tab->index >= 0
        && tab->index < tab->widget->count())
    {
        tab->widget->removeTab(tab->index);
    }
    return 0;
}},
       {"title",
        [](lua_State *L) -> int
{
    auto *tab = static_cast<TabHandle *>(luaL_checkudata(L, 1, "TabMetaTable"));
    if (tab && tab->widget && tab->index >= 0
        && tab->index < tab->widget->count())
    {
        QString title = tab->widget->tabText(tab->index);
        lua_pushstring(L, title.toUtf8().constData());
        return 1;
    }
    else
    {
        lua_pushnil(L);
        return 1;
    }
}},
       {"rename",
        [](lua_State *L) -> int
{
    auto *tab = static_cast<TabHandle *>(luaL_checkudata(L, 1, "TabMetaTable"));
    if (tab && tab->widget && tab->index >= 0
        && tab->index < tab->widget->count())
    {
        const QString title = lua_isnoneornil(L, 2)
                                  ? QString()
                                  : QString::fromUtf8(luaL_checkstring(L, 2));
        if (auto *lektra = qobject_cast<Lektra *>(tab->widget->window()))
            lektra->renameTab(tab->index, title);
    }
    return 0;
}},
       {"index",
        [](lua_State *L) -> int
{
    auto *tab = static_cast<TabHandle *>(luaL_checkudata(L, 1, "TabMetaTable"));
    if (tab && tab->widget && tab->index >= 0
        && tab->index < tab->widget->count())
    {
        lua_pushinteger(L, tab->index);
        return 1;
    }
    else
    {
        lua_pushnil(L);
        return 1;
    }
}},

       {"container",
        [](lua_State *L) -> int
{
    auto *tab = static_cast<TabHandle *>(luaL_checkudata(L, 1, "TabMetaTable"));
    if (tab && tab->widget && tab->index >= 0
        && tab->index < tab->widget->count())
    {
        auto *container = tab->widget->container(tab->index);
        if (container)
        {
            auto **ud = static_cast<DocumentContainer **>(
                lua_newuserdata(L, sizeof(DocumentContainer *)));
            *ud = container;
            luaL_getmetatable(L, "ContainerMetaTable");
            lua_setmetatable(L, -2);
            return 1;
        }
        else
        {
            lua_pushnil(L);
            return 1;
        }
    }
    else
    {
        lua_pushnil(L);
        return 1;
    }
}},

       {"view",
        [](lua_State *L) -> int
{
    auto *tab = static_cast<TabHandle *>(luaL_checkudata(L, 1, "TabMetaTable"));
    if (tab && tab->widget && tab->index >= 0
        && tab->index < tab->widget->count())
    {
        auto *container = tab->widget->container(tab->index);
        if (container)
        {
            auto *view = container->view();
            if (view)
            {
                auto **ud = static_cast<DocumentView **>(
                    lua_newuserdata(L, sizeof(DocumentView *)));
                *ud = view;
                luaL_getmetatable(L, "DocumentViewMetaTable");
                lua_setmetatable(L, -2);
                return 1;
            }
            else
            {
                lua_pushnil(L);
                return 1;
            }
        }
        else
        {
            lua_pushnil(L);
            return 1;
        }
    }
    else
    {
        lua_pushnil(L);
    }

    return 1;
}},
       {nullptr, nullptr}};

static void
registerTab(lua_State *L)
{
    // 1. Create the metatable
    luaL_newmetatable(L, "TabMetaTable");

    // 2. Set __index to itself
    // This trick means: "if a key isn't in the userdata, look in this
    // metatable"
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    // 3. Register the methods into the metatable
    luaL_setfuncs(L, TabMethods, 0);

    lua_pop(L, 1); // Pop the metatable
}
} // namespace

void
Lektra::initLuaTabs() noexcept
{
    registerTab(m_L);
    lua_newtable(m_L);

    lua_pushlightuserdata(m_L, this);

    // lektra.tabs.close(index)
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        int index = luaL_optinteger(L, 1, -1);
        if (index < -1)
            return luaL_error(L, "Invalid tab index: %d", index);

        lektra->Tab_close(index);
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "close");

    // lektra.tabs.goto(index)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        int index = luaL_optinteger(L, 1, -1);
        if (index < -1)
            return luaL_error(L, "Invalid tab index: %d", index);

        lektra->Tab_goto(index);

        return 0;
    }, 1);
    lua_setfield(m_L, -2, "goto");

    // lektra.tabs.last()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->Tab_last();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "last");

    // lektra.tabs.first()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->Tab_first();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "first");

    // lektra.tabs.next()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->Tab_next();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "next");

    // lektra.tabs.prev()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->Tab_prev();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "prev");

    // lektra.tabs.move_right()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->TabMoveRight();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "move_right");

    // lektra.tabs.move_left()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lektra->TabMoveLeft();
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "move_left");

    // lektra.tabs.count()
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!lektra->m_tab_widget)
        {
            lua_pushinteger(L, 0);
            return 1;
        }

        lua_pushinteger(L, lektra->m_tab_widget->count());
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "count");

    // lektra.tabs.current() -> Tab Object
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        auto *tab_widget = lektra->m_tab_widget;
        if (!tab_widget)
        {
            lua_pushnil(L);
            return 1;
        }

        int current_index = tab_widget->currentIndex();
        if (current_index == -1)
        {
            lua_pushnil(L);
            return 1;
        }

        // --- MAGIC HAPPENS HERE ---
        // 1. Create a Full Userdata (it's a box that holds the tab handle)
        auto *udata
            = static_cast<TabHandle *>(lua_newuserdata(L, sizeof(TabHandle)));
        udata->widget = tab_widget;
        udata->index  = current_index;

        // 2. Attach the Metatable so Lua knows this "box" has methods like
        // :id()
        luaL_getmetatable(L, "TabMetaTable");
        lua_setmetatable(L, -2);

        return 1;
    }, 1);
    lua_setfield(m_L, -2, "current");

    // lektra.tabs.list() - returns Tab[]
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        auto *tab_widget = lektra->m_tab_widget;
        if (!tab_widget)
        {
            lua_pushnil(L);
            return 1;
        }

        lua_newtable(L);
        for (int i = 0; i < tab_widget->count(); ++i)
        {
            auto *udata = static_cast<TabHandle *>(
                lua_newuserdata(L, sizeof(TabHandle)));
            udata->widget = tab_widget;
            udata->index  = i;

            luaL_getmetatable(L, "TabMetaTable");
            lua_setmetatable(L, -2);

            lua_rawseti(L, -2, i + 1);
        }

        return 1;
    }, 1);
    lua_setfield(m_L, -2, "list");

    // lektra.tabs.get_id(index)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        int index = luaL_optinteger(L, 1, -1);
        if (index < -1)
            return luaL_error(L, "Invalid tab index: %d", index);

        int tab_id = lektra->m_tab_widget->id(index);
        if (tab_id == -1)
        {
            lua_pushnil(L);
        }
        else
        {
            lua_pushinteger(L, tab_id);
        }
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "get_id");

    // Multi-tab selection and operations. Indices are 0-based. Operations
    // take an optional list of indices; by default they act on the selected
    // tabs, or on the current tab when nothing is selected.
    auto setFn = [this](const char *name, lua_CFunction fn)
    {
        lua_pushlightuserdata(m_L, this);
        lua_pushcclosure(m_L, fn, 1);
        lua_setfield(m_L, -2, name);
    };

    // Reads an optional index list at stack position `idx`, keeping only
    // indices that refer to an existing tab.
    static auto readIndices = [](lua_State *L, int idx, Lektra *lektra)
    {
        QList<int> out;
        if (!lektra->m_tab_widget)
            return out;
        if (lua_istable(L, idx))
        {
            const int count     = lektra->m_tab_widget->count();
            const lua_Integer n = luaL_len(L, idx);
            for (lua_Integer i = 1; i <= n; ++i)
            {
                lua_rawgeti(L, idx, i);
                const int v = static_cast<int>(luaL_checkinteger(L, -1));
                lua_pop(L, 1);
                if (v >= 0 && v < count)
                    out << v;
            }
        }
        else
        {
            out = lektra->targetTabs();
        }
        return out;
    };

    // lektra.tabs.selected() -> integer[]
    setFn("selected", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        lua_newtable(L);
        if (!lektra->m_tab_widget)
            return 1;
        int i = 1;
        for (int index : lektra->m_tab_widget->tabBar()->selectedTabs())
        {
            lua_pushinteger(L, index);
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });

    // lektra.tabs.select(index, selected = true)
    setFn("select", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!lektra->m_tab_widget)
            return 0;
        const int index = static_cast<int>(luaL_checkinteger(L, 1));
        const bool selected
            = lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2);
        lektra->m_tab_widget->tabBar()->setTabSelected(index, selected);
        return 0;
    });

    setFn("select_all", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!lektra->m_tab_widget)
            return 0;
        lektra->m_tab_widget->tabBar()->selectAllTabs();
        return 0;
    });

    setFn("clear_selection", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!lektra->m_tab_widget)
            return 0;
        lektra->m_tab_widget->tabBar()->clearTabSelection();
        return 0;
    });

    // lektra.tabs.close_selected([indices])
    setFn("close_selected", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!lektra->m_tab_widget)
            return 0;
        lektra->closeTabs(readIndices(L, 1, lektra));
        return 0;
    });

    // lektra.tabs.merge(mode, [indices]) — mode is "vertical" or "horizontal"
    setFn("merge", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!lektra->m_tab_widget)
            return 0;
        const QString mode = QString::fromUtf8(luaL_checkstring(L, 1));
        if (mode != QLatin1String("vertical")
            && mode != QLatin1String("horizontal"))
            return luaL_error(
                L, "tabs.merge: mode must be \"vertical\" or \"horizontal\"");
        lektra->mergeTabsAsSplits(readIndices(L, 2, lektra),
                                  mode == QLatin1String("vertical"));
        return 0;
    });

    // lektra.tabs.split_out([indices]) and its other name split_to_tabs:
    // returns how many splits were moved into tabs of their own
    auto splitToTabs = [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!lektra->m_tab_widget)
            return 0;
        lua_pushinteger(L,
                        lektra->splitTabsIntoTabs(readIndices(L, 1, lektra)));
        return 1;
    };
    setFn("split_out", splitToTabs);
    setFn("split_to_tabs", splitToTabs);

    // lektra.tabs.split_to_windows([indices]) -> how many splits were moved
    setFn("split_to_windows", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!lektra->m_tab_widget)
            return 0;
        lua_pushinteger(
            L, lektra->splitTabsToWindows(readIndices(L, 1, lektra)));
        return 1;
    });

    // lektra.tabs.move_to_window([indices])
    setFn("move_to_window", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!lektra->m_tab_widget)
            return 0;
        lektra->moveTabsToNewWindow(readIndices(L, 1, lektra));
        return 0;
    });

    // lektra.tabs.save_session(name, [indices]) — name may be nil to be asked
    setFn("save_session", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!lektra->m_tab_widget)
            return 0;
        const QString name = lua_isnoneornil(L, 1)
                                 ? QString()
                                 : QString::fromUtf8(luaL_checkstring(L, 1));
        lektra->saveTabsAsSession(readIndices(L, 2, lektra), name);
        return 0;
    });

    // lektra.tabs.rename(index, title) — nil or "" restores the default title
    setFn("rename", [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        const int index     = static_cast<int>(luaL_checkinteger(L, 1));
        const QString title = lua_isnoneornil(L, 2)
                                  ? QString()
                                  : QString::fromUtf8(luaL_checkstring(L, 2));
        lektra->renameTab(index, title);
        return 0;
    });

    lua_setfield(m_L, -2, "tabs");
}
