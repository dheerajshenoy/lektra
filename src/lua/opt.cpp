#include "Config.hpp"
#include "ConfigOptions.hpp"
#include "DocumentView.hpp"
#include "Lektra.hpp"
#include "StatusbarLayoutSpec.hpp"
#include "utils.hpp"

#include <QKeySequence>
#include <algorithm>
#include <string>
#include <variant>
#include <cmath>
#include <cstring>
#include <lauxlib.h>
#include <lua.h>

namespace
{
namespace co = config_options;

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
        {"Vertical", (int)DocumentView::LayoutMode::VERTICAL},
        {"Horizontal", (int)DocumentView::LayoutMode::HORIZONTAL},
        {"Single", (int)DocumentView::LayoutMode::SINGLE},
        {"Book", (int)DocumentView::LayoutMode::BOOK},
        {"Grid", (int)DocumentView::LayoutMode::GRID},
    };

    static const LuaEnumEntry fitMode[] = {
        {"Width", (int)DocumentView::FitMode::Width},
        {"Height", (int)DocumentView::FitMode::Height},
        {"Window", (int)DocumentView::FitMode::Window},
    };

    static const LuaEnumEntry mouseButton[] = {
        {"Left", (int)Qt::MouseButton::LeftButton},
        {"Right", (int)Qt::MouseButton::RightButton},
        {"Middle", (int)Qt::MouseButton::MiddleButton},
    };

    static const LuaEnumEntry backend[] = {
        {"Auto", (int)Config::Rendering::Backend::Auto},
        {"Raster", (int)Config::Rendering::Backend::Raster},
        {"OpenGL", (int)Config::Rendering::Backend::OpenGL},
    };

    registerLuaEnum(L, "LayoutMode", layoutMode, std::size(layoutMode));
    registerLuaEnum(L, "FitMode", fitMode, std::size(fitMode));
    registerLuaEnum(L, "MouseButton", mouseButton, std::size(mouseButton));
    registerLuaEnum(L, "Backend", backend, std::size(backend));
}

// Lua values as plain Qt data and back, for options with a free shape
// (statusbar.layout): a table with only 1..n keys is a list, any other table a
// map with text keys.
static QVariant
luaToVariant(lua_State *L, int idx, int depth = 0)
{
    idx = lua_absindex(L, idx);
    if (depth > 8)
        return {};
    switch (lua_type(L, idx))
    {
        case LUA_TBOOLEAN:
            return static_cast<bool>(lua_toboolean(L, idx));
        case LUA_TNUMBER:
        {
            const double d = lua_tonumber(L, idx);
            if (d == std::floor(d) && std::abs(d) < 1e15)
                return static_cast<qlonglong>(d);
            return d;
        }
        case LUA_TSTRING:
            return QString::fromUtf8(lua_tostring(L, idx));
        case LUA_TTABLE:
        {
            const int length = static_cast<int>(lua_rawlen(L, idx));
            int keys         = 0;
            for (lua_pushnil(L); lua_next(L, idx) != 0; lua_pop(L, 1))
                ++keys;
            if (keys == length)
            {
                QVariantList list;
                for (int i = 1; i <= length; ++i)
                {
                    lua_rawgeti(L, idx, i);
                    list << luaToVariant(L, -1, depth + 1);
                    lua_pop(L, 1);
                }
                return list;
            }
            QVariantMap map;
            for (lua_pushnil(L); lua_next(L, idx) != 0; lua_pop(L, 1))
                if (lua_type(L, -2) == LUA_TSTRING)
                    map.insert(QString::fromUtf8(lua_tostring(L, -2)),
                               luaToVariant(L, -1, depth + 1));
            return map;
        }
        default:
            return {};
    }
}

static void
variantToLua(lua_State *L, const QVariant &v)
{
    switch (v.typeId())
    {
        case QMetaType::Bool:
            lua_pushboolean(L, v.toBool());
            break;
        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
            lua_pushinteger(L, v.toLongLong());
            break;
        case QMetaType::Double:
        case QMetaType::Float:
            lua_pushnumber(L, v.toDouble());
            break;
        case QMetaType::QVariantList:
        case QMetaType::QStringList:
        {
            lua_newtable(L);
            int i = 1;
            for (const QVariant &item : v.toList())
            {
                variantToLua(L, item);
                lua_rawseti(L, -2, i++);
            }
            break;
        }
        case QMetaType::QVariantMap:
        {
            lua_newtable(L);
            const QVariantMap map = v.toMap();
            for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            {
                variantToLua(L, it.value());
                lua_setfield(L, -2, it.key().toUtf8().constData());
            }
            break;
        }
        case QMetaType::QString:
            lua_pushstring(L, v.toString().toUtf8().constData());
            break;
        default:
            lua_pushnil(L);
            break;
    }
}

// picker.keys: each action takes one key ("Up") or a list of them
// ({"Up", "Ctrl+P"}), in the same notation as [picker.keys] in config.toml.
static int
pushKeyList(lua_State *L, const QList<QKeyCombination> &keys)
{
    lua_newtable(L);
    int i = 1;
    for (const QKeyCombination &kc : keys)
    {
        const QString text
            = QKeySequence(kc).toString(QKeySequence::PortableText);
        lua_pushstring(L, text.toUtf8().constData());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

static void
readKeyList(lua_State *L, int idx, QList<QKeyCombination> &keys)
{
    auto parse = [L](const char *text) -> QKeyCombination
    {
        const QKeySequence seq = QKeySequence::fromString(
            QString::fromUtf8(text), QKeySequence::PortableText);
        if (seq.isEmpty())
            luaL_error(L, "picker.keys: '%s' is not a key", text);
        return seq[0];
    };

    QList<QKeyCombination> out;
    if (lua_type(L, idx) == LUA_TSTRING)
        out << parse(lua_tostring(L, idx));
    else if (lua_istable(L, idx))
    {
        const lua_Integer n = luaL_len(L, idx);
        for (lua_Integer i = 1; i <= n; ++i)
        {
            lua_rawgeti(L, idx, i);
            if (lua_type(L, -1) != LUA_TSTRING)
                luaL_error(L, "picker.keys: keys are strings like \"Ctrl+K\"");
            out << parse(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    else
        luaL_error(L, "picker.keys: expected a key or a list of keys");

    if (out.isEmpty())
        luaL_error(L, "picker.keys: at least one key is needed");
    keys = out;
}

// ---------------------------------------------------------------------------
// Options with a shape of their own (Kind::Custom in ConfigOptions.cpp)
// ---------------------------------------------------------------------------

struct LuaCustom
{
    const char *path;
    // pushes the value
    void (*get)(lua_State *L, Config &c, Lektra *lk);
    // reads the value at `idx`
    void (*set)(lua_State *L, int idx, Config &c, Lektra *lk);
    // false: the value lives outside Config (picker keys)
    bool inConfig = true;
};

#define PICKER_KEY(NAME, MEMBER)                                               \
    {"picker.keys." NAME,                                                      \
     [](lua_State *L, Config &, Lektra *lk)                                    \
    { pushKeyList(L, lk->pickerKeybindings().MEMBER); },                       \
     [](lua_State *L, int idx, Config &, Lektra *lk)                           \
    { readKeyList(L, idx, lk->pickerKeybindings().MEMBER); }, false}

static const LuaCustom kCustom[] = {
    {"window.initial_size",
     [](lua_State *L, Config &c, Lektra *)
{
    lua_newtable(L);
    lua_pushinteger(L, c.window.initial_size[0]);
    lua_rawseti(L, -2, 1);
    lua_pushinteger(L, c.window.initial_size[1]);
    lua_rawseti(L, -2, 2);
},
     [](lua_State *L, int idx, Config &c, Lektra *)
{
    if (!lua_istable(L, idx))
        luaL_error(L, "Expected a table for initial_size");
    lua_rawgeti(L, idx, 1);
    lua_rawgeti(L, idx, 2);
    if (!lua_isinteger(L, -2) || !lua_isinteger(L, -1))
        luaL_error(L, "Expected integer values in initial_size table");
    c.window.initial_size[0] = static_cast<int>(lua_tointeger(L, -2));
    c.window.initial_size[1] = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 2);
}},

    {"window.title_format",
     [](lua_State *L, Config &c, Lektra *)
{ lua_pushstring(L, c.window.title_format.toUtf8().constData()); },
     [](lua_State *L, int idx, Config &c, Lektra *)
{ c.window.title_format = QString::fromUtf8(luaL_checkstring(L, idx)); }},

    {"statusbar.layout",
     [](lua_State *L, Config &c, Lektra *)
{ variantToLua(L, c.statusbar.layout); },
     [](lua_State *L, int idx, Config &c, Lektra *)
{
    if (lua_istable(L, idx))
        c.statusbar.layout
            = statusbar_layout::fromVariant(luaToVariant(L, idx));
}},

    // a list of four numbers (left, top, right, bottom), or one for all
    {"statusbar.padding",
     [](lua_State *L, Config &c, Lektra *)
{
    lua_newtable(L);
    for (int i = 0; i < 4; ++i)
    {
        lua_pushinteger(L, c.statusbar.padding[i]);
        lua_rawseti(L, -2, i + 1);
    }
},
     [](lua_State *L, int idx, Config &c, Lektra *)
{
    auto &padding = c.statusbar.padding;
    if (lua_istable(L, idx))
    {
        for (int i = 0; i < 4; ++i)
        {
            lua_rawgeti(L, idx, i + 1);
            padding[i] = static_cast<int>(lua_tointeger(L, -1));
            lua_pop(L, 1);
        }
    }
    else if (lua_isnumber(L, idx))
        padding.fill(static_cast<int>(lua_tointeger(L, idx)));
}},

    PICKER_KEY("accept", accept),
    PICKER_KEY("collapse", collapse),
    PICKER_KEY("dismiss", dismiss),
    PICKER_KEY("down", moveDown),
    PICKER_KEY("expand", expand),
    PICKER_KEY("history_next", historyNext),
    PICKER_KEY("history_prev", historyPrev),
    PICKER_KEY("page_down", pageDown),
    PICKER_KEY("page_up", pageUp),
    PICKER_KEY("section_next", sectionNext),
    PICKER_KEY("section_prev", sectionPrev),
    PICKER_KEY("toggle_structure_mode", toggleStructureMode),
    PICKER_KEY("up", moveUp),

    // one number for every screen, or a table keyed by screen name
    {"rendering.dpr",
     [](lua_State *L, Config &c, Lektra *)
{
    using DPR = Config::Rendering::DPR;
    const DPR &dpr = c.rendering.dpr;
    if (std::holds_alternative<float>(dpr))
    {
        lua_pushnumber(L, std::get<float>(dpr));
        return;
    }
    lua_newtable(L);
    const auto &map = std::get<QMap<QString, float>>(dpr);
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        lua_pushstring(L, it.key().toUtf8().constData());
        lua_pushnumber(L, it.value());
        lua_settable(L, -3);
    }
},
     [](lua_State *L, int idx, Config &c, Lektra *)
{
    if (lua_isnumber(L, idx))
        c.rendering.dpr = static_cast<float>(lua_tonumber(L, idx));
    else if (lua_istable(L, idx))
    {
        QMap<QString, float> map;
        for (lua_pushnil(L); lua_next(L, idx) != 0; lua_pop(L, 1))
            if (lua_isstring(L, -2) && lua_isnumber(L, -1))
                map.insert(QString::fromUtf8(lua_tostring(L, -2)),
                           static_cast<float>(lua_tonumber(L, -1)));
        c.rendering.dpr = map;
    }
}},

    {"preview.size_ratio",
     [](lua_State *L, Config &c, Lektra *)
{
    lua_newtable(L);
    lua_pushnumber(L, c.preview.size_ratio[0]);
    lua_setfield(L, -2, "width");
    lua_pushnumber(L, c.preview.size_ratio[1]);
    lua_setfield(L, -2, "height");
},
     [](lua_State *L, int idx, Config &c, Lektra *)
{
    if (!lua_istable(L, idx))
        return;
    lua_getfield(L, idx, "width");
    lua_getfield(L, idx, "height");
    c.preview.size_ratio[0] = static_cast<float>(lua_tonumber(L, -2));
    c.preview.size_ratio[1] = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 2);
}},

    {"llm_view.extra_body",
     [](lua_State *L, Config &c, Lektra *)
{
    lua_newtable(L);
    const auto &map = c.llm_view.extra_body;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        switch (it.value().typeId())
        {
            case QMetaType::Bool:
                lua_pushboolean(L, it.value().toBool());
                break;
            case QMetaType::Int:
            case QMetaType::LongLong:
                lua_pushinteger(L, it.value().toLongLong());
                break;
            case QMetaType::Double:
                lua_pushnumber(L, it.value().toDouble());
                break;
            default:
                lua_pushstring(L, it.value().toString().toUtf8().constData());
                break;
        }
        lua_setfield(L, -2, it.key().toUtf8().constData());
    }
},
     [](lua_State *L, int idx, Config &c, Lektra *)
{
    if (!lua_istable(L, idx))
        return;
    auto &map = c.llm_view.extra_body;
    map.clear();
    for (lua_pushnil(L); lua_next(L, idx) != 0; lua_pop(L, 1))
    {
        const QString key = QString::fromUtf8(lua_tostring(L, -2));
        if (lua_isboolean(L, -1))
            map.insert(key, static_cast<bool>(lua_toboolean(L, -1)));
        else if (lua_isinteger(L, -1))
            map.insert(key, static_cast<qlonglong>(lua_tointeger(L, -1)));
        else if (lua_isnumber(L, -1))
            map.insert(key, lua_tonumber(L, -1));
        else if (lua_isstring(L, -1))
            map.insert(key, QString::fromUtf8(lua_tostring(L, -1)));
    }
}},

    // a LayoutMode value, or nil to keep the current layout
    {"presentation.layout",
     [](lua_State *L, Config &c, Lektra *)
{
    if (c.presentation.layout)
        lua_pushinteger(L, static_cast<int>(*c.presentation.layout));
    else
        lua_pushnil(L);
},
     [](lua_State *L, int idx, Config &c, Lektra *)
{
    if (lua_isnil(L, idx))
        c.presentation.layout = std::nullopt;
    else
        c.presentation.layout
            = static_cast<DocumentView::LayoutMode>(lua_tointeger(L, idx));
}},

    {"presentation.allowed_actions",
     [](lua_State *L, Config &c, Lektra *)
{
    const auto &list = c.presentation.allowed_actions;
    lua_createtable(L, static_cast<int>(list.size()), 0);
    int i = 1;
    for (const QString &s : list)
    {
        lua_pushstring(L, s.toUtf8().constData());
        lua_rawseti(L, -2, i++);
    }
},
     [](lua_State *L, int idx, Config &c, Lektra *)
{
    auto &list = c.presentation.allowed_actions;
    list.clear();
    if (!lua_istable(L, idx))
        return;
    const int n = static_cast<int>(lua_rawlen(L, idx));
    for (int i = 1; i <= n; ++i)
    {
        lua_rawgeti(L, idx, i);
        if (lua_isstring(L, -1))
            list << QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}},

    {"misc.color_dialog_colors",
     [](lua_State *L, Config &c, Lektra *)
{
    lua_newtable(L);
    const auto &colors = c.misc.color_dialog_colors;
    for (size_t i = 0; i < colors.size(); ++i)
    {
        lua_pushstring(L, colors[i].name(QColor::HexArgb).toUtf8().constData());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
},
     [](lua_State *L, int idx, Config &c, Lektra *)
{
    if (!lua_istable(L, idx))
        return;
    auto &colors = c.misc.color_dialog_colors;
    colors.clear();
    const int n = static_cast<int>(luaL_len(L, idx));
    for (int i = 1; i <= n; ++i)
    {
        lua_rawgeti(L, idx, i);
        colors.push_back(QColor(QString::fromUtf8(lua_tostring(L, -1))));
        lua_pop(L, 1);
    }
}},
};

#undef PICKER_KEY

static const LuaCustom *
findCustom(std::string_view path)
{
    for (const LuaCustom &c : kCustom)
        if (path == c.path)
            return &c;
    return nullptr;
}

// ---------------------------------------------------------------------------
// The option tables
//
// lektra.opt, lektra.opt_global and view:opt() are proxy tables: each holds
// the path it stands for ("" for the top, "annotations.rect", ...) and looks
// every key up in the option list (ConfigOptions.cpp). A key below the path
// that is an option is read or written; one that has options below it is a
// further proxy.
// ---------------------------------------------------------------------------

// Raw (non-metamethod) access to a proxy's own bookkeeping keys; the
// metamethod-aware lua_getfield/lua_setfield would recurse into optIndex.
static void
rawGetField(lua_State *L, int idx, const char *key)
{
    const int abs = lua_absindex(L, idx);
    lua_pushstring(L, key);
    lua_rawget(L, abs);
}

// Expects the value on top of the stack (consumes it), like lua_setfield.
static void
rawSetField(lua_State *L, int idx, const char *key)
{
    const int abs = lua_absindex(L, idx);
    lua_pushstring(L, key);
    lua_insert(L, -2);
    lua_rawset(L, abs);
}

struct Proxy
{
    std::string prefix;
    int view_id; // -1 lektra.opt, -2 lektra.opt_global, else view:opt()
    Lektra *lektra;

    // only the options a view has of its own are in view:opt()
    unsigned scope() const noexcept
    {
        return view_id >= 0 ? co::PerView : co::Global;
    }

    std::string path(const char *key) const
    {
        return prefix.empty() ? std::string(key) : prefix + "." + key;
    }
};

static Proxy
proxyAt(lua_State *L, int idx)
{
    Proxy p;
    rawGetField(L, idx, "__prefix");
    p.prefix = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    rawGetField(L, idx, "__view_id");
    p.view_id = static_cast<int>(lua_tointeger(L, -1));
    rawGetField(L, idx, "__lektra");
    p.lektra = static_cast<Lektra *>(lua_touserdata(L, -1));
    lua_pop(L, 3);
    return p;
}

static Config &
proxyConfig(lua_State *L, const Proxy &p)
{
    Config *config = p.lektra ? p.lektra->luaOptConfig(p.view_id) : nullptr;
    if (!config)
        luaL_error(L, "option table belongs to a view that was closed");
    return *config;
}

// A problem with a value written from Lua: reported, and the value is left
// as it was.
static void
optWarning(lua_State *L, Lektra *lektra, const QString &message)
{
    luaL_where(L, 1);
    const QString text
        = QString::fromUtf8(lua_tostring(L, -1)) + QStringLiteral("opt.")
          + message;
    lua_pop(L, 1);
    qWarning().noquote() << "lua:" << text;
    if (lektra)
        lektra->showMessage(text, 5.0f, MessageLog::Level::Warning);
    else
        logWarning(text);
}

static void pushProxy(lua_State *L, const std::string &prefix, int viewId,
                      Lektra *lektra);

static void
pushOption(lua_State *L, const co::Option &o, Config &c, Lektra *lektra)
{
    switch (o.kind)
    {
        case co::Kind::Bool:
            lua_pushboolean(L, o.ref<bool>(c));
            break;
        case co::Kind::Int:
            lua_pushinteger(L, o.ref<int>(c));
            break;
        case co::Kind::Float:
            lua_pushnumber(L, o.ref<float>(c));
            break;
        case co::Kind::Text:
            lua_pushstring(L, o.ref<QString>(c).toUtf8().constData());
            break;
        case co::Kind::Color:
            lua_pushinteger(L, static_cast<lua_Integer>(o.ref<uint32_t>(c)));
            break;
        case co::Kind::Choice:
        {
            const int value = o.getChoice(c);
            if (o.luaNumber)
                lua_pushinteger(L, value);
            else if (const char *name = o.choiceName(value))
                lua_pushstring(L, name);
            else
                lua_pushnil(L);
            break;
        }
        case co::Kind::Custom:
            if (const LuaCustom *custom = findCustom(o.path))
                custom->get(L, c, lektra);
            else
                lua_pushnil(L);
            break;
    }
}

// Writes the value at `idx` into `o` in `c`. Returns false (after a warning
// unless `quiet`) if the value has the wrong type.
static bool
writeOption(lua_State *L, int idx, const co::Option &o, Config &c,
            Lektra *lektra, bool quiet = false)
{
    auto wrong = [&](const QString &expected)
    {
        if (!quiet)
            optWarning(L, lektra,
                       QStringLiteral("%1: expected %2, got %3")
                           .arg(QString::fromStdString(o.path), expected,
                                QString::fromUtf8(luaL_typename(L, idx))));
        return false;
    };

    switch (o.kind)
    {
        case co::Kind::Bool:
            if (lua_type(L, idx) != LUA_TBOOLEAN)
                return wrong(QStringLiteral("true or false"));
            o.ref<bool>(c) = lua_toboolean(L, idx);
            return true;

        case co::Kind::Int:
            if (!lua_isnumber(L, idx))
                return wrong(QStringLiteral("a whole number"));
            o.ref<int>(c) = static_cast<int>(std::clamp<lua_Integer>(
                lua_tointeger(L, idx), o.rangeMin, o.rangeMax));
            return true;

        case co::Kind::Float:
            if (!lua_isnumber(L, idx))
                return wrong(QStringLiteral("a number"));
            o.ref<float>(c) = static_cast<float>(lua_tonumber(L, idx));
            return true;

        case co::Kind::Text:
            if (!lua_isstring(L, idx))
                return wrong(QStringLiteral("a string"));
            o.ref<QString>(c) = QString::fromUtf8(lua_tostring(L, idx));
            return true;

        case co::Kind::Color:
        {
            uint32_t value = 0;
            if (lua_type(L, idx) == LUA_TNUMBER)
                value = static_cast<uint32_t>(lua_tointeger(L, idx));
            else if (lua_type(L, idx) != LUA_TSTRING
                     || !parseHexColor(lua_tostring(L, idx), value))
                return wrong(QStringLiteral("a color like \"#RRGGBB\""));
            o.ref<uint32_t>(c) = value;
            return true;
        }

        case co::Kind::Choice:
        {
            int value = 0;
            if (o.luaNumber && lua_type(L, idx) == LUA_TNUMBER)
            {
                value = static_cast<int>(lua_tointeger(L, idx));
                if (!o.choiceName(value))
                    return wrong(QStringLiteral("a valid enum value"));
            }
            else if (lua_type(L, idx) != LUA_TSTRING
                     || !o.choiceValue(lua_tostring(L, idx), value))
                return wrong(o.choiceList());
            o.setChoice(c, value);
            return true;
        }

        case co::Kind::Custom:
            if (const LuaCustom *custom = findCustom(o.path))
                custom->set(L, idx, c, lektra);
            return true;
    }
    return false;
}

// The child proxy for `key` of the proxy at `idx`, cached in its
// __children table. (Not stored on the proxy itself: a raw key there would
// skip optNewIndex, and `opt.search = {...}` would replace the table.)
static void
pushChild(lua_State *L, int idx, const char *key, const Proxy &p)
{
    idx = lua_absindex(L, idx);
    rawGetField(L, idx, "__children");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        rawSetField(L, idx, "__children");
    }
    lua_getfield(L, -1, key);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        pushProxy(L, p.path(key), p.view_id, p.lektra);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, key);
    }
    lua_remove(L, -2); // the __children table
}

static int
optIndex(lua_State *L)
{
    const char *key = lua_tostring(L, 2);
    if (!key)
    {
        lua_pushnil(L);
        return 1;
    }
    const Proxy p          = proxyAt(L, 1);
    const std::string path = p.path(key);

    if (const co::Option *o = co::find(path);
        o && (o->scope & p.scope()) == p.scope())
    {
        pushOption(L, *o, proxyConfig(L, p), p.lektra);
        return 1;
    }
    if (co::hasChildren(path, p.scope()))
    {
        pushChild(L, 1, key, p);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

// A write to an option. lektra.opt changes the global default and the
// current view, like Vim's ":set"; lektra.opt_global only the default
// (":setglobal"); view:opt() only that view (":setlocal").
//
// Assigning a table to a sub-table (`opt.search = {...}`) sets each of its
// keys in turn, so the real options are reached.
static int
optNewIndex(lua_State *L)
{
    const char *key = lua_tostring(L, 2);
    if (!key)
        return 0;
    const Proxy p          = proxyAt(L, 1);
    const std::string path = p.path(key);

    if (const co::Option *o = co::find(path);
        o && (o->scope & p.scope()) == p.scope())
    {
        if (!writeOption(L, 3, *o, proxyConfig(L, p), p.lektra))
            return 0;

        const QString section = QString::fromStdString(o->section);
        if (p.view_id < 0)
        {
            if (p.view_id == -1)
            {
                if (DocumentView *doc = p.lektra->currentDocument())
                {
                    const LuaCustom *custom = o->kind == co::Kind::Custom
                                                  ? findCustom(o->path)
                                                  : nullptr;
                    if (!custom || custom->inConfig)
                        writeOption(L, 3, *o, doc->localConfig(), p.lektra,
                                    true);
                    doc->localConfigChanged(section);
                }
            }
            if (o->onChange)
                o->onChange(p.lektra);
        }
        else if (DocumentView *view = p.lektra->get_view_by_id(p.view_id))
        {
            view->localConfigChanged(section);
        }
        return 0;
    }

    if (co::hasChildren(path, p.scope()))
    {
        if (!lua_istable(L, 3))
        {
            optWarning(L, p.lektra,
                       QStringLiteral("%1 is a table of options; set one of "
                                      "them, or assign a table")
                           .arg(QString::fromStdString(path)));
            return 0;
        }
        pushChild(L, 1, key, p);
        const int child = lua_absindex(L, -1);
        for (lua_pushnil(L); lua_next(L, 3) != 0; lua_pop(L, 1))
        {
            lua_pushvalue(L, -2); // key
            lua_pushvalue(L, -2); // value
            lua_settable(L, child);
        }
        lua_pop(L, 1); // child
        return 0;
    }

    optWarning(L, p.lektra,
               QStringLiteral("%1: no such option")
                   .arg(QString::fromStdString(path)));
    return 0;
}

static void
pushProxy(lua_State *L, const std::string &prefix, int viewId, Lektra *lektra)
{
    lua_newtable(L);
    lua_pushstring(L, prefix.c_str());
    rawSetField(L, -2, "__prefix");
    lua_pushinteger(L, viewId);
    rawSetField(L, -2, "__view_id");
    lua_pushlightuserdata(L, lektra);
    rawSetField(L, -2, "__lektra");

    if (luaL_newmetatable(L, "lektra.opt"))
    {
        lua_pushcfunction(L, optIndex);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, optNewIndex);
        lua_setfield(L, -2, "__newindex");
    }
    lua_setmetatable(L, -2);
}
} // namespace


void
Lektra::initLuaOpt() noexcept
{
    initLuaEnums(m_L);
    pushProxy(m_L, {}, -1, this);
    lua_setfield(m_L, -2, "opt");
    // The same tables, but a write only changes the global default.
    pushProxy(m_L, {}, -2, this);
    lua_setfield(m_L, -2, "opt_global");
}

// view:opt() — the same option tables, bound to one view's local copy.
void
Lektra::pushViewOptTable(lua_State *L, DocumentView *view) noexcept
{
    pushProxy(L, {}, view->id(), this);
}

bool
Lektra::resetViewOptions(DocumentView *view, const QString &section) noexcept
{
    if (!view)
        return false;

    // the sections a view has of its own (the ones view:opt() lists)
    struct ViewSection
    {
        const char *name;
        void (*copy)(Config &to, const Config &from);
    };
    static const ViewSection sections[] = {
        {"page", [](Config &to, const Config &from) { to.page = from.page; }},
        {"search",
         [](Config &to, const Config &from) { to.search = from.search; }},
        {"annotations", [](Config &to, const Config &from)
    { to.annotations = from.annotations; }},
        {"layout",
         [](Config &to, const Config &from) { to.layout = from.layout; }},
        {"reflow",
         [](Config &to, const Config &from) { to.reflow = from.reflow; }},
        {"zoom", [](Config &to, const Config &from) { to.zoom = from.zoom; }},
        {"selection",
         [](Config &to, const Config &from) { to.selection = from.selection; }},
        {"split",
         [](Config &to, const Config &from) { to.split = from.split; }},
        {"scrollbars", [](Config &to, const Config &from)
    { to.scrollbars = from.scrollbars; }},
        {"minimap", [](Config &to, const Config &from)
    { to.minimap = from.minimap; }},
        {"jump_marker", [](Config &to, const Config &from)
    { to.jump_marker = from.jump_marker; }},
        {"links",
         [](Config &to, const Config &from) { to.links = from.links; }},
        {"link_hints", [](Config &to, const Config &from)
    { to.link_hints = from.link_hints; }},
        {"rendering",
         [](Config &to, const Config &from) { to.rendering = from.rendering; }},
        {"behavior",
         [](Config &to, const Config &from) { to.behavior = from.behavior; }},
    };

    const Config fresh = view->freshLocalConfig();
    Config &local      = view->localConfig();

    if (section.isEmpty())
    {
        for (const ViewSection &s : sections)
        {
            s.copy(local, fresh);
            view->localConfigChanged(QString::fromLatin1(s.name));
        }
        return true;
    }

    for (const ViewSection &s : sections)
    {
        if (section == QLatin1String(s.name))
        {
            s.copy(local, fresh);
            view->localConfigChanged(section);
            return true;
        }
    }
    return false;
}

Config *
Lektra::luaOptConfig(int viewId) noexcept
{
    if (viewId < 0)
        return &m_config;
    DocumentView *view = get_view_by_id(viewId);
    return view ? &view->localConfig() : nullptr;
}
