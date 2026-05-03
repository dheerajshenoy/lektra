#include "Config.hpp"
#include "Lektra.hpp"
#include "utils.hpp"

#include <lua.h>

namespace
{
struct LuaField
{
    const char *key;
    int (*get)(lua_State *, void *);
    void (*set)(lua_State *, void *);
    void (*callback)(Lektra *) = nullptr; // called after set, nullptr = no-op
};

struct LuaEnumEntry
{
    const char *key;
    int value;
};

static void
registerLuaEnum(lua_State *L, const char *name, const LuaEnumEntry *entries,
                int count)
{
    lua_createtable(L, 0, count); // pre-sized, no rehash
    for (int i = 0; i < count; i++)
    {
        lua_pushinteger(L, entries[i].value);
        lua_setfield(L, -2, entries[i].key); // faster than lua_settable
    }
    lua_setfield(L, -2, name);
}

static void
initLuaEnums(lua_State *L)
{
    static const LuaEnumEntry layoutMode[] = {
        {"VERTICAL", (int)DocumentView::LayoutMode::VERTICAL},
        {"HORIZONTAL", (int)DocumentView::LayoutMode::HORIZONTAL},
        {"SINGLE", (int)DocumentView::LayoutMode::SINGLE},
        {"BOOK", (int)DocumentView::LayoutMode::BOOK},
    };
    static const LuaEnumEntry fitMode[] = {
        {"WIDTH", (int)DocumentView::FitMode::Width},
        {"HEIGHT", (int)DocumentView::FitMode::Height},
        {"WINDOW", (int)DocumentView::FitMode::Window},
    };
    static const LuaEnumEntry mouseButton[] = {
        {"LEFT", (int)Qt::MouseButton::LeftButton},
        {"RIGHT", (int)Qt::MouseButton::RightButton},
        {"MIDDLE", (int)Qt::MouseButton::MiddleButton},
    };

    registerLuaEnum(L, "LayoutMode", layoutMode, std::size(layoutMode));
    registerLuaEnum(L, "FitMode", fitMode, std::size(fitMode));
    registerLuaEnum(L, "MouseButton", mouseButton, std::size(mouseButton));
}

using P = void *;

// --- page ---
static const LuaField pageFields[] = {
    {"bg",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Page *>(p)->bg);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Page *>(p)->bg = lua_tointeger(L, 3); },
    [] (Lektra *lektra){

    }},
    {"fg",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Page *>(p)->fg);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Page *>(p)->fg = lua_tointeger(L, 3); }},
};

// --- synctex ---
static const LuaField synctexFields[] = {
    {"editor_command",
     [](lua_State *L, P p)
{
    lua_pushstring(
        L,
        static_cast<Config::Synctex *>(p)->editor_command.toUtf8().constData());
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Synctex *>(p)->editor_command = lua_tostring(L, 3); }},
    {"enabled",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Synctex *>(p)->enabled);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Synctex *>(p)->enabled = lua_toboolean(L, 3); }},
};

// --- search ---
static const LuaField searchFields[] = {
    {"absolute_jump",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Search *>(p)->absolute_jump);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Search *>(p)->absolute_jump = lua_toboolean(L, 3); }},
    {"highlight_matches",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Search *>(p)->highlight_matches);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Search *>(p)->highlight_matches = lua_toboolean(L, 3); }},
    {"index_color",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Search *>(p)->index_color);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Search *>(p)->index_color = lua_tointeger(L, 3); }},
    {"match_color",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Search *>(p)->match_color);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Search *>(p)->match_color = lua_tointeger(L, 3); }},
    {"progressive",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Search *>(p)->progressive);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Search *>(p)->progressive = lua_toboolean(L, 3); }},
};

// --- annotations.highlight ---
static const LuaField annotHighlightFields[] = {
    {"color",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Annotations::Highlight *>(p)->color);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Highlight *>(p)->color
        = lua_tointeger(L, 3);
}},
    {"comment",
     [](lua_State *L, P p)
{
    lua_pushboolean(L,
                    static_cast<Config::Annotations::Highlight *>(p)->comment);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Highlight *>(p)->comment
        = lua_toboolean(L, 3);
}},
    {"comment_font_size",
     [](lua_State *L, P p)
{
    lua_pushinteger(
        L, static_cast<Config::Annotations::Highlight *>(p)->comment_font_size);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Highlight *>(p)->comment_font_size
        = lua_tointeger(L, 3);
}},
    {"comment_marker",
     [](lua_State *L, P p)
{
    lua_pushboolean(
        L, static_cast<Config::Annotations::Highlight *>(p)->comment_marker);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Highlight *>(p)->comment_marker
        = lua_toboolean(L, 3);
}},
    {"glow_color",
     [](lua_State *L, P p)
{
    lua_pushinteger(
        L, static_cast<Config::Annotations::Highlight *>(p)->glow_color);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Highlight *>(p)->glow_color
        = lua_tointeger(L, 3);
}},
    {"glow_width",
     [](lua_State *L, P p)
{
    lua_pushinteger(
        L, static_cast<Config::Annotations::Highlight *>(p)->glow_width);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Highlight *>(p)->glow_width
        = lua_tointeger(L, 3);
}},
    {"hover_glow",
     [](lua_State *L, P p)
{
    lua_pushboolean(
        L, static_cast<Config::Annotations::Highlight *>(p)->hover_glow);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Highlight *>(p)->hover_glow
        = lua_toboolean(L, 3);
}},
};

// --- annotations.rect ---
static const LuaField annotRectFields[] = {
    {"color",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Annotations::Rect *>(p)->color);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Annotations::Rect *>(p)->color = lua_tointeger(L, 3); }},
    {"comment",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Annotations::Rect *>(p)->comment);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Rect *>(p)->comment = lua_toboolean(L, 3);
}},
    {"comment_font_size",
     [](lua_State *L, P p)
{
    lua_pushinteger(
        L, static_cast<Config::Annotations::Rect *>(p)->comment_font_size);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Rect *>(p)->comment_font_size
        = lua_tointeger(L, 3);
}},
    {"comment_marker",
     [](lua_State *L, P p)
{
    lua_pushboolean(
        L, static_cast<Config::Annotations::Rect *>(p)->comment_marker);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Rect *>(p)->comment_marker
        = lua_toboolean(L, 3);
}},
    {"glow_color",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Annotations::Rect *>(p)->glow_color);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Rect *>(p)->glow_color
        = lua_tointeger(L, 3);
}},
    {"glow_width",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Annotations::Rect *>(p)->glow_width);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Rect *>(p)->glow_width
        = lua_tointeger(L, 3);
}},
    {"hover_glow",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Annotations::Rect *>(p)->hover_glow);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Rect *>(p)->hover_glow
        = lua_toboolean(L, 3);
}},
};

// --- annotations.popup ---
static const LuaField annotPopupFields[] = {
    {"color",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Annotations::Rect *>(p)->color);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Annotations::Rect *>(p)->color = lua_tointeger(L, 3); }},
    {"comment",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Annotations::Rect *>(p)->comment);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Rect *>(p)->comment = lua_toboolean(L, 3);
}},
    {"comment_font_size",
     [](lua_State *L, P p)
{
    lua_pushinteger(
        L, static_cast<Config::Annotations::Rect *>(p)->comment_font_size);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Rect *>(p)->comment_font_size
        = lua_tointeger(L, 3);
}},
    {"glow_color",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Annotations::Rect *>(p)->glow_color);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Rect *>(p)->glow_color
        = lua_tointeger(L, 3);
}},
    {"glow_width",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Annotations::Rect *>(p)->glow_width);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Rect *>(p)->glow_width
        = lua_tointeger(L, 3);
}},
    {"hover_glow",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Annotations::Rect *>(p)->hover_glow);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Annotations::Rect *>(p)->hover_glow
        = lua_toboolean(L, 3);
}},
};

// --- thumbnail panel ---
static const LuaField thumbnailPanelFields[] = {
    {"show_page_numbers",
     [](lua_State *L, P p)
{
    lua_pushboolean(
        L, static_cast<Config::ThumbnailPanel *>(p)->show_page_numbers);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Page *>(p)->bg = lua_tointeger(L, 3); }},
    {"panel_width",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::ThumbnailPanel *>(p)->panel_width);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Page *>(p)->fg = lua_tointeger(L, 3); }},
};

// --- portal ---
static const LuaField portalFields[] = {
    {"border_color",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Portal *>(p)->border_color);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Portal *>(p)->border_color = lua_tointeger(L, 3); }},
    {"border_width",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Portal *>(p)->border_width);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Portal *>(p)->border_width = lua_tointeger(L, 3); }},
    {"dim_inactive",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Portal *>(p)->dim_inactive);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Portal *>(p)->dim_inactive = lua_toboolean(L, 3); }},
    {"respect_parent",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Portal *>(p)->respect_parent);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Portal *>(p)->respect_parent = lua_toboolean(L, 3); }},
    {"enabled",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Portal *>(p)->enabled);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Portal *>(p)->enabled = lua_toboolean(L, 3); }},
};

// --- window ---
static const LuaField windowFields[] = {
    {"bg",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Window *>(p)->bg);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Window *>(p)->bg = lua_tointeger(L, 3); }},
    {"accent",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Window *>(p)->accent);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Window *>(p)->accent = lua_tointeger(L, 3); }},
    {"fullscreen",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Window *>(p)->fullscreen);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Window *>(p)->fullscreen = lua_toboolean(L, 3); }},
    {"menubar",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Window *>(p)->menubar);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Window *>(p)->menubar = lua_toboolean(L, 3); }},
    {"startup_tab",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Window *>(p)->startup_tab);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Window *>(p)->startup_tab = lua_toboolean(L, 3); }},
    {"title_format",
     [](lua_State *L, P p)
{
    lua_pushstring(
        L, static_cast<Config::Window *>(p)->title_format.toUtf8().constData());
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Window *>(p)->title_format = lua_tostring(L, 3); }},
    {"initial_size",
     [](lua_State *L, P p)
{
    lua_newtable(L);
    lua_pushinteger(L, static_cast<Config::Window *>(p)->initial_size[0]);
    lua_rawseti(L, -2, 1);
    lua_pushinteger(L, static_cast<Config::Window *>(p)->initial_size[1]);
    lua_rawseti(L, -2, 2);
    return 1;
},
     [](lua_State *L, P p)
{
    if (!lua_istable(L, 3))
    {
        luaL_error(L, "Expected a table for initial_size");
        return;
    }
    lua_rawgeti(L, 3, 1);
    lua_rawgeti(L, 3, 2);
    if (!lua_isinteger(L, -2) || !lua_isinteger(L, -1))
    {
        luaL_error(L, "Expected integer values in initial_size table");
        return;
    }
    static_cast<Config::Window *>(p)->initial_size[0] = lua_tointeger(L, -2);
    static_cast<Config::Window *>(p)->initial_size[1] = lua_tointeger(L, -1);
    lua_pop(L, 2);
}},
};

// --- layout ---
static const LuaField layoutFields[] = {
    {"initial_fit",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, (int)static_cast<Config::Layout *>(p)->initial_fit);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Layout *>(p)->initial_fit
        = (DocumentView::FitMode)lua_tointeger(L, 3);
}},
    {"auto_resize",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Layout *>(p)->auto_resize);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Layout *>(p)->auto_resize = lua_toboolean(L, 3); }},
    {"mode",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, (int)static_cast<Config::Layout *>(p)->mode);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Layout *>(p)->mode
        = (DocumentView::LayoutMode)lua_tointeger(L, 3);
}},
};

// --- statusbar ---
static const LuaField statusbarFields[] = {
    {"padding",
     [](lua_State *L, P p)
{
    lua_newtable(L);
    for (int i = 0; i < 4; ++i)
    {
        lua_pushinteger(L, static_cast<Config::Statusbar *>(p)->padding[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
},
     [](lua_State *L, P p)
{
    if (lua_istable(L, 3))
    {
        for (int i = 0; i < 4; ++i)
        {
            lua_rawgeti(L, 3, i + 1);
            static_cast<Config::Statusbar *>(p)->padding[i]
                = lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
    }
}},
    {"visible",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Statusbar *>(p)->visible);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Statusbar *>(p)->visible = lua_toboolean(L, 3); }},
};

// --- zoom ---
static const LuaField zoomFields[] = {
    {"anchor_to_mouse",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Zoom *>(p)->anchor_to_mouse);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Zoom *>(p)->anchor_to_mouse = lua_toboolean(L, 3); }},
    {"factor",
     [](lua_State *L, P p)
{
    lua_pushnumber(L, static_cast<Config::Zoom *>(p)->factor);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Zoom *>(p)->factor = lua_tonumber(L, 3); }},
    {"level",
     [](lua_State *L, P p)
{
    lua_pushnumber(L, static_cast<Config::Zoom *>(p)->level);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Zoom *>(p)->level = lua_tonumber(L, 3); }},
};

// --- selection ---
static const LuaField selectionFields[] = {
    {"color",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Selection *>(p)->color);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Selection *>(p)->color = lua_tointeger(L, 3); }},
    {"copy_on_select",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Selection *>(p)->copy_on_select);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Selection *>(p)->copy_on_select = lua_toboolean(L, 3); }},
    {"drag_threshold",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Selection *>(p)->drag_threshold);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Selection *>(p)->drag_threshold = lua_tointeger(L, 3); }},
};

// --- split ---
static const LuaField splitFields[] = {
    {"dim_inactive",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Split *>(p)->dim_inactive);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Split *>(p)->dim_inactive = lua_toboolean(L, 3); }},
    {"dim_inactive_opacity",
     [](lua_State *L, P p)
{
    lua_pushnumber(L, static_cast<Config::Split *>(p)->dim_inactive_opacity);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Split *>(p)->dim_inactive_opacity = lua_tonumber(L, 3);
}},
    {"focus_follows_mouse",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Split *>(p)->focus_follows_mouse);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Split *>(p)->focus_follows_mouse = lua_toboolean(L, 3);
}},
    {"mouse_follows_focus",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Split *>(p)->mouse_follows_focus);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Split *>(p)->mouse_follows_focus = lua_toboolean(L, 3);
}},
};

// --- scrollbars ---
static const LuaField scrollbarsFields[] = {
    {"auto_hide",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Scrollbars *>(p)->auto_hide);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Scrollbars *>(p)->auto_hide = lua_toboolean(L, 3); }},
    {"hide_timeout",
     [](lua_State *L, P p)
{
    lua_pushnumber(L, static_cast<Config::Scrollbars *>(p)->hide_timeout);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Scrollbars *>(p)->hide_timeout = lua_tonumber(L, 3); }},
    {"horizontal",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Scrollbars *>(p)->horizontal);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Scrollbars *>(p)->horizontal = lua_toboolean(L, 3); }},
    {"search_hits",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Scrollbars *>(p)->search_hits);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Scrollbars *>(p)->search_hits = lua_toboolean(L, 3); }},
    {"size",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Scrollbars *>(p)->size);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Scrollbars *>(p)->size = lua_tointeger(L, 3); }},
    {"vertical",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Scrollbars *>(p)->vertical);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Scrollbars *>(p)->vertical = lua_toboolean(L, 3); }},
};

// --- jump_marker ---
static const LuaField jumpMarkerFields[] = {
    {"color",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::JumpMarker *>(p)->color);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::JumpMarker *>(p)->color = lua_tointeger(L, 3); }},
    {"enabled",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::JumpMarker *>(p)->enabled);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::JumpMarker *>(p)->enabled = lua_toboolean(L, 3); }},
    {"fade_duration",
     [](lua_State *L, P p)
{
    lua_pushnumber(L, static_cast<Config::JumpMarker *>(p)->fade_duration);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::JumpMarker *>(p)->fade_duration = lua_tonumber(L, 3); }},
};

// --- links ---
static const LuaField linksFields[] = {
    {"boundary",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Links *>(p)->boundary);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Links *>(p)->boundary = lua_toboolean(L, 3); }},
    {"detect_urls",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Links *>(p)->detect_urls);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Links *>(p)->detect_urls = lua_toboolean(L, 3); }},
    {"enabled",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Links *>(p)->enabled);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Links *>(p)->enabled = lua_toboolean(L, 3); }},
    {"url_regex",
     [](lua_State *L, P p)
{
    lua_pushstring(
        L, static_cast<Config::Links *>(p)->url_regex.toUtf8().constData());
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Links *>(p)->url_regex
        = QString::fromUtf8(lua_tostring(L, 3));
}},
};

// --- link_hints ---
static const LuaField linkHintsFields[] = {
    {"bg",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Link_hints *>(p)->bg);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Link_hints *>(p)->bg = lua_tointeger(L, 3); }},
    {"fg",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Link_hints *>(p)->fg);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Link_hints *>(p)->fg = lua_tointeger(L, 3); }},
    {"size",
     [](lua_State *L, P p)
{
    lua_pushnumber(L, static_cast<Config::Link_hints *>(p)->size);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Link_hints *>(p)->size = lua_tonumber(L, 3); }},
};

// --- tabs ---
static const LuaField tabsFields[] = {
    {"auto_hide",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Tabs *>(p)->auto_hide);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Tabs *>(p)->auto_hide = lua_toboolean(L, 3); }},
    {"closable",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Tabs *>(p)->closable);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Tabs *>(p)->closable = lua_toboolean(L, 3); }},
    {"full_path",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Tabs *>(p)->full_path);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Tabs *>(p)->full_path = lua_toboolean(L, 3); }},
    {"lazy_load",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Tabs *>(p)->lazy_load);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Tabs *>(p)->lazy_load = lua_toboolean(L, 3); }},
    {"movable",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Tabs *>(p)->movable);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Tabs *>(p)->movable = lua_toboolean(L, 3); }},
    {"visible",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Tabs *>(p)->visible);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Tabs *>(p)->visible = lua_toboolean(L, 3); }},
};

// --- picker ---
static const LuaField pickerFields[] = {
    {"width",
     [](lua_State *L, P p)
{
    lua_pushnumber(L, static_cast<Config::Picker *>(p)->width);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Picker *>(p)->width = lua_tonumber(L, 3); }},
    {"height",
     [](lua_State *L, P p)
{
    lua_pushnumber(L, static_cast<Config::Picker *>(p)->height);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Picker *>(p)->height = lua_tonumber(L, 3); }},
    {"border",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Picker *>(p)->border);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Picker *>(p)->border = lua_toboolean(L, 3); }},
    {"alternating_row_color",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Picker *>(p)->alternating_row_color);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Picker *>(p)->alternating_row_color
        = lua_toboolean(L, 3);
}},
    // shadow is a sub-struct, handled via pushSection in init
};

// --- picker.shadow ---
static const LuaField pickerShadowFields[] = {
    {"enabled",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Picker *>(p)->shadow.enabled);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Picker *>(p)->shadow.enabled = lua_toboolean(L, 3); }},
    {"blur_radius",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Picker *>(p)->shadow.blur_radius);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Picker *>(p)->shadow.blur_radius = lua_tointeger(L, 3);
}},
    {"offset_x",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Picker *>(p)->shadow.offset_x);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Picker *>(p)->shadow.offset_x = lua_tointeger(L, 3); }},
    {"offset_y",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Picker *>(p)->shadow.offset_y);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Picker *>(p)->shadow.offset_y = lua_tointeger(L, 3); }},
    {"opacity",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Picker *>(p)->shadow.opacity);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Picker *>(p)->shadow.opacity = lua_tointeger(L, 3); }},
};

// --- outline (Inherits Picker) ---
static const LuaField outlineFields[] = {
    {"indent_width",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Outline *>(p)->indent_width);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Outline *>(p)->indent_width = lua_tointeger(L, 3); }},
    {"show_page_number",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Outline *>(p)->show_page_number);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Outline *>(p)->show_page_number = lua_toboolean(L, 3); }},
    {"flat_menu",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Outline *>(p)->flat_menu);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Outline *>(p)->flat_menu = lua_toboolean(L, 3); }},
    // Note: Picker fields (width, height, etc) should be added here or handled
    // via a shared base mapper
};

// --- highlight_search (Inherits Picker) ---
static const LuaField highlightSearchFields[] = {
    {"flat_menu",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::HighlightSearch *>(p)->flat_menu);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::HighlightSearch *>(p)->flat_menu = lua_toboolean(L, 3);
}},
};

// --- command_palette (Inherits Picker) ---
static const LuaField commandPaletteFields[] = {
    {"prompt",
     [](lua_State *L, P p)
{
    lua_pushstring(
        L,
        static_cast<Config::CommandPalette *>(p)->prompt.toUtf8().constData());
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::CommandPalette *>(p)->prompt
        = QString::fromUtf8(lua_tostring(L, 3));
}},
    {"vscrollbar",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::CommandPalette *>(p)->vscrollbar);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::CommandPalette *>(p)->vscrollbar = lua_toboolean(L, 3);
}},
    {"show_shortcuts",
     [](lua_State *L, P p)
{
    lua_pushboolean(L,
                    static_cast<Config::CommandPalette *>(p)->show_shortcuts);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::CommandPalette *>(p)->show_shortcuts
        = lua_toboolean(L, 3);
}},
    {"description",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::CommandPalette *>(p)->description);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::CommandPalette *>(p)->description = lua_toboolean(L, 3);
}},
};

// --- rendering ---
static const LuaField renderingFields[] = {
    {"antialiasing",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Rendering *>(p)->antialiasing);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Rendering *>(p)->antialiasing = lua_toboolean(L, 3); }},
    {"antialiasing_bits",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Rendering *>(p)->antialiasing_bits);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Rendering *>(p)->antialiasing_bits
        = lua_tointeger(L, 3);
}},
    {"text_antialiasing",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Rendering *>(p)->text_antialiasing);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Rendering *>(p)->text_antialiasing
        = lua_toboolean(L, 3);
}},
    {"smooth_pixmap_transform",
     [](lua_State *L, P p)
{
    lua_pushboolean(
        L, static_cast<Config::Rendering *>(p)->smooth_pixmap_transform);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Rendering *>(p)->smooth_pixmap_transform
        = lua_toboolean(L, 3);
}},
};

// --- behavior ---
static const LuaField behaviorFields[] = {
    {"confirm_on_quit",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Behavior *>(p)->confirm_on_quit);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Behavior *>(p)->confirm_on_quit = lua_toboolean(L, 3); }},
    {"undo_limit",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Behavior *>(p)->undo_limit);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Behavior *>(p)->undo_limit = lua_tointeger(L, 3); }},
    {"cache_pages",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Behavior *>(p)->cache_pages);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Behavior *>(p)->cache_pages = lua_tointeger(L, 3); }},
    {"preload_pages",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Behavior *>(p)->preload_pages);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Behavior *>(p)->preload_pages = lua_tointeger(L, 3); }},
    {"auto_reload",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Behavior *>(p)->auto_reload);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Behavior *>(p)->auto_reload = lua_toboolean(L, 3); }},
    {"invert_mode",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Behavior *>(p)->invert_mode);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Behavior *>(p)->invert_mode = lua_toboolean(L, 3); }},
    {"dont_invert_images",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Behavior *>(p)->dont_invert_images);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Behavior *>(p)->dont_invert_images
        = lua_toboolean(L, 3);
}},
    {"open_last_visited",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Behavior *>(p)->open_last_visited);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Behavior *>(p)->open_last_visited = lua_toboolean(L, 3);
}},
    {"single_instance",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Behavior *>(p)->single_instance);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Behavior *>(p)->single_instance = lua_toboolean(L, 3); }},
    {"remember_last_visited",
     [](lua_State *L, P p)
{
    lua_pushboolean(L,
                    static_cast<Config::Behavior *>(p)->remember_last_visited);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Behavior *>(p)->remember_last_visited
        = lua_toboolean(L, 3);
}},
    {"recent_files",
     [](lua_State *L, P p)
{
    lua_pushboolean(L, static_cast<Config::Behavior *>(p)->recent_files);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Behavior *>(p)->recent_files = lua_toboolean(L, 3); }},
    {"num_recent_files",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Behavior *>(p)->num_recent_files);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Behavior *>(p)->num_recent_files = lua_tointeger(L, 3);
}},
    {"page_history_limit",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Behavior *>(p)->page_history_limit);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Behavior *>(p)->page_history_limit
        = lua_tointeger(L, 3);
}},
};

// --- preview ---
static const LuaField previewFields[] = {
    {"size_ratio",
     [](lua_State *L, P p)
{
    lua_newtable(L);
    lua_pushnumber(L, static_cast<Config::Preview *>(p)->size_ratio[0]);
    lua_setfield(L, -2, "width");
    lua_pushnumber(L, static_cast<Config::Preview *>(p)->size_ratio[1]);
    lua_setfield(L, -2, "height");
    return 1;
},
     [](lua_State *L, P p)
{
    if (lua_istable(L, 3))
    {
        lua_getfield(L, 3, "width");
        static_cast<Config::Preview *>(p)->size_ratio[0] = lua_tonumber(L, -1);
        lua_getfield(L, 3, "height");
        static_cast<Config::Preview *>(p)->size_ratio[1] = lua_tonumber(L, -1);
        lua_pop(L, 2);
    }
}},
    {"border_radius",
     [](lua_State *L, P p)
{
    lua_pushinteger(L, static_cast<Config::Preview *>(p)->border_radius);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Preview *>(p)->border_radius = lua_tointeger(L, 3); }},
    {"close_on_click_outside",
     [](lua_State *L, P p)
{
    lua_pushboolean(L,
                    static_cast<Config::Preview *>(p)->close_on_click_outside);
    return 1;
},
     [](lua_State *L, P p)
{
    static_cast<Config::Preview *>(p)->close_on_click_outside
        = lua_toboolean(L, 3);
}},
    {"opacity",
     [](lua_State *L, P p)
{
    lua_pushnumber(L, static_cast<Config::Preview *>(p)->opacity);
    return 1;
}, [](lua_State *L, P p)
{ static_cast<Config::Preview *>(p)->opacity = lua_tonumber(L, 3); }},
};

// --- misc ---
static const LuaField miscFields[] = {
    {"color_dialog_colors",
     [](lua_State *L, P p)
{
    lua_newtable(L);
    auto &colors = static_cast<Config::Misc *>(p)->color_dialog_colors;
    for (size_t i = 0; i < colors.size(); ++i)
    {
        lua_pushstring(L, colors[i].name(QColor::HexArgb).toUtf8().constData());
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
},
     [](lua_State *L, P p)
{
    if (lua_istable(L, 3))
    {
        auto &colors = static_cast<Config::Misc *>(p)->color_dialog_colors;
        colors.clear();
        int n = luaL_len(L, 3);
        for (int i = 1; i <= n; ++i)
        {
            lua_rawgeti(L, 3, i);
            colors.push_back(QColor(lua_tostring(L, -1)));
            lua_pop(L, 1);
        }
    }
}},
};

static const LuaField *
findField(const LuaField *fields, int count, const char *key)
{
    int lo = 0, hi = count - 1;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(fields[mid].key, key);
        if (cmp == 0)
            return &fields[mid];
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return nullptr;
}

static int
genericIndex(lua_State *L)
{
    lua_getfield(L, 1, "__ptr");
    void *ptr = lua_touserdata(L, -1);
    lua_getfield(L, 1, "__fields");
    auto *fields = static_cast<const LuaField *>(lua_touserdata(L, -1));
    lua_getfield(L, 1, "__count");
    int count = lua_tointeger(L, -1);
    lua_pop(L, 3);

    const LuaField *f = findField(fields, count, lua_tostring(L, 2));
    if (f)
        return f->get(L, ptr);
    lua_pushnil(L);
    return 1;
}

static int
genericNewIndex(lua_State *L)
{
    lua_getfield(L, 1, "__ptr");
    void *ptr = lua_touserdata(L, -1);
    lua_getfield(L, 1, "__fields");
    auto *fields = static_cast<const LuaField *>(lua_touserdata(L, -1));
    lua_getfield(L, 1, "__count");
    int count = lua_tointeger(L, -1);
    lua_getfield(L, 1, "__lektra");
    auto *lektra = static_cast<Lektra *>(lua_touserdata(L, -1));
    lua_pop(L, 4);

    const LuaField *f = findField(fields, count, lua_tostring(L, 2));
    if (f)
    {
        f->set(L, ptr);
        if (f->callback && lektra)
        {
            f->callback(lektra);
        }
    }
    return 0;
}

template <int N>
static void
pushSection(lua_State *L, void *ptr, const LuaField (&fields)[N],
            Lektra *lektra)
{
    lua_newtable(L);
    lua_pushlightuserdata(L, ptr);
    lua_setfield(L, -2, "__ptr");
    lua_pushlightuserdata(L, (void *)fields);
    lua_setfield(L, -2, "__fields");
    lua_pushinteger(L, N);
    lua_setfield(L, -2, "__count");
    lua_pushlightuserdata(L, lektra);
    lua_setfield(L, -2, "__lektra");

    lua_newtable(L);
    lua_pushcfunction(L, genericIndex);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, genericNewIndex);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    // caller is responsible for lua_setfield into parent
}

static void
initLuaSections(lua_State *L, Config &config, Lektra *lektra)
{
    // lektra.opt {}
    lua_newtable(L);

    // lektra.opt.page
    pushSection(L, &config.page, pageFields, lektra);
    lua_setfield(L, -2, "page");

    // lektra.opt.synctex
    pushSection(L, &config.synctex, synctexFields, lektra);
    lua_setfield(L, -2, "synctex");

    // lektra.opt.search
    pushSection(L, &config.search, searchFields, lektra);
    lua_setfield(L, -2, "search");

    // lektra.opt.annotations {}
    lua_newtable(L);

    // lektra.opt.annotations.highlight
    pushSection(L, &config.annotations.highlight, annotHighlightFields, lektra);
    lua_setfield(L, -2, "highlight");

    // lektra.opt.annotations.rect
    pushSection(L, &config.annotations.rect, annotRectFields, lektra);
    lua_setfield(L, -2, "rect");

    // lektra.opt.annotations.popup
    pushSection(L, &config.annotations.popup, annotPopupFields, lektra);
    lua_setfield(L, -2, "popup");

    // lektra.opt.annotations
    lua_setfield(L, -2, "annotations");

    // lektra.opt.thumbnail_panel
    pushSection(L, &config.thumbnail, thumbnailPanelFields, lektra);
    lua_setfield(L, -2, "thumbnail_panel");

    // lektra.opt.portal
    pushSection(L, &config.portal, portalFields, lektra);
    lua_setfield(L, -2, "portal");

    // lektra.opt.window
    pushSection(L, &config.window, windowFields, lektra);
    lua_setfield(L, -2, "window");

    // lektra.opt.layout
    pushSection(L, &config.layout, layoutFields, lektra);
    lua_setfield(L, -2, "layout");

    // lektra.opt.statusbar
    pushSection(L, &config.statusbar, statusbarFields, lektra);
    lua_setfield(L, -2, "statusbar");

    // lektra.opt.zoom
    pushSection(L, &config.zoom, zoomFields, lektra);
    lua_setfield(L, -2, "zoom");

    // lektra.opt.selection
    pushSection(L, &config.selection, selectionFields, lektra);
    lua_setfield(L, -2, "selection");

    // lektra.opt.split
    pushSection(L, &config.split, splitFields, lektra);
    lua_setfield(L, -2, "split");

    // lektra.opt.scrollbars
    pushSection(L, &config.scrollbars, scrollbarsFields, lektra);
    lua_setfield(L, -2, "scrollbars");

    // lektra.opt.jump_marker
    pushSection(L, &config.jump_marker, jumpMarkerFields, lektra);
    lua_setfield(L, -2, "jump_marker");

    // lektra.opt.links
    pushSection(L, &config.links, linksFields, lektra);
    lua_setfield(L, -2, "links");

    // lektra.opt.link_hints
    pushSection(L, &config.link_hints, linkHintsFields, lektra);
    lua_setfield(L, -2, "link_hints");

    // lektra.opt.tabs
    pushSection(L, &config.tabs, tabsFields, lektra);
    lua_setfield(L, -2, "tabs");

    // lektra.opt.picker
    pushSection(L, &config.picker, pickerFields, lektra);
    lua_setfield(L, -2, "picker");

    // lektra.opt.picker.shadow
    // pushSection(L, &config.picker.shadow, pickerShadowFields);
    // lua_setfield(L, -2, "picker_shadow");

    // lektra.opt.outline
    pushSection(L, &config.outline, outlineFields, lektra);
    lua_setfield(L, -2, "outline");

    // lektra.opt.highlight_search
    pushSection(L, &config.highlight_search, highlightSearchFields, lektra);
    lua_setfield(L, -2, "highlight_search");

    // lektra.opt.command_palette
    pushSection(L, &config.command_palette, commandPaletteFields, lektra);
    lua_setfield(L, -2, "command_palette");

    // lektra.opt.rendering
    pushSection(L, &config.rendering, renderingFields, lektra);
    lua_setfield(L, -2, "rendering");

    // lektra.opt.behavior
    pushSection(L, &config.behavior, behaviorFields, lektra);
    lua_setfield(L, -2, "behavior");

    // lektra.opt.preview
    pushSection(L, &config.preview, previewFields, lektra);
    lua_setfield(L, -2, "preview");

    // lektra.opt.misc
    pushSection(L, &config.misc, miscFields, lektra);
    lua_setfield(L, -2, "misc");

    // lektra.opt
    lua_setfield(L, -2, "opt");
}
} // namespace

void
Lektra::initLuaOpt() noexcept
{
    initLuaEnums(m_L);
    initLuaSections(m_L, m_config, this);
}
