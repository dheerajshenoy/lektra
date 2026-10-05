#include "Lektra.hpp"
#include "Statusbar.hpp"
#include "StatusbarLayoutSpec.hpp"

#include <QHash>
#include <QTimer>

// lektra.statusbar: pieces of text a script puts in the statusbar.
//
//   register(name, provider, [opts])  provider is a function(view) that
//                                     returns the text (or nil to hide the
//                                     segment, or { text=, tooltip= }), or a
//                                     fixed string. It is asked again when the
//                                     statusbar changes (page, zoom, file...)
//                                     and every `interval` seconds if given.
//                                     opts: { interval=, tooltip=, on_click= }
//   set(name, text, [opts])           shows `text` (nil or "" hides it); opts
//                                     as above, without interval
//   update([name])                    asks the provider(s) again now
//   unregister(name)                  removes the segment
//
// A segment is a module like "page" or "zoom": it is placed where the layout
// names it, or at the right end if the layout does not.

namespace
{
struct Segment
{
    int provider = LUA_NOREF;
    int onClick  = LUA_NOREF;
    QTimer *timer = nullptr;
    QString text, tooltip; // what `set` showed
    QString lastError;     // so that one error is not printed again and again
};

class Bridge : public QObject
{
public:
    Bridge(Lektra *lektra, lua_State *L) : QObject(lektra), lektra(lektra), L(L)
    {
        setObjectName(QStringLiteral("lektraStatusbar"));
    }

    Lektra *lektra;
    lua_State *L;
    QHash<QString, Segment> segments;
    bool connected = false;
    bool pending   = false;
    bool dead      = false; // Lua is going away: do not call into it any more
};

// Raises the error for a name a segment cannot have.
void
checkName(lua_State *L, const QString &name)
{
    if (!statusbar_layout::isModuleName(name))
        luaL_error(L,
                   "\"%s\" is not a segment name: use letters, digits, _ - and .",
                   qPrintable(name));
    if (statusbar_layout::modules().contains(name))
        luaL_error(L, "\"%s\" is one of the built-in modules", qPrintable(name));
}

Bridge *
bridgeOf(lua_State *L)
{
    return static_cast<Bridge *>(lua_touserdata(L, lua_upvalueindex(1)));
}

QString
nameArg(lua_State *L, int index)
{
    size_t length    = 0;
    const char *text = luaL_checklstring(L, index, &length);
    return QString::fromUtf8(text, static_cast<qsizetype>(length))
        .trimmed()
        .toLower();
}

void
dropRefs(Bridge *b, Segment &segment)
{
    if (segment.provider != LUA_NOREF)
        luaL_unref(b->L, LUA_REGISTRYINDEX, segment.provider);
    if (segment.onClick != LUA_NOREF)
        luaL_unref(b->L, LUA_REGISTRYINDEX, segment.onClick);
    segment.provider = segment.onClick = LUA_NOREF;
    delete segment.timer;
    segment.timer = nullptr;
}

void
pushCurrentView(Bridge *b)
{
    DocumentView *view = b->lektra->currentDocument();
    if (!view)
    {
        lua_pushnil(b->L);
        return;
    }
    auto **ud = static_cast<DocumentView **>(
        lua_newuserdata(b->L, sizeof(DocumentView *)));
    *ud = view;
    luaL_getmetatable(b->L, "DocumentViewMetaTable");
    lua_setmetatable(b->L, -2);
}

// Asks the provider of a segment (if it has one) and shows the result.
void
evaluate(Bridge *b, const QString &name)
{
    if (b->dead || !b->lektra->statusbar())
        return;

    auto it = b->segments.find(name);
    if (it == b->segments.end())
        return;

    // The provider is a script: it may add or remove segments, so nothing of
    // the segment is kept across the call.
    const int provider = it->provider;
    QString text       = it->text;
    QString tooltip    = it->tooltip;
    QString error;

    if (provider != LUA_NOREF)
    {
        lua_State *L = b->L;
        const int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, provider);
        pushCurrentView(b);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        {
            error = QString::fromUtf8(lua_tostring(L, -1));
            text.clear();
            tooltip.clear();
        }
        else if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "text");
            text = lua_isstring(L, -1) ? QString::fromUtf8(lua_tostring(L, -1))
                                       : QString();
            lua_pop(L, 1);
            lua_getfield(L, -1, "tooltip");
            tooltip = lua_isstring(L, -1) ? QString::fromUtf8(lua_tostring(L, -1))
                                          : QString();
            lua_pop(L, 1);
        }
        else if (lua_isstring(L, -1))
        {
            text = QString::fromUtf8(lua_tostring(L, -1));
        }
        else
        {
            text.clear(); // nil or false hides the segment
        }
        lua_settop(L, top);
    }

    it = b->segments.find(name);
    if (it == b->segments.end())
        return;
    if (error != it->lastError && !error.isEmpty())
        fprintf(stderr, "Lua error in statusbar segment \"%s\": %s\n",
                qPrintable(name), qPrintable(error));
    it->lastError = error;

    b->lektra->statusbar()->setCustomModule(name, text, tooltip,
                                            it->onClick != LUA_NOREF);
}

void
refreshAll(Bridge *b)
{
    const QStringList names = b->segments.keys();
    for (const QString &name : names)
        evaluate(b, name);
}

// Many changes come together (a page change moves several things): ask the
// providers once, when they are done.
void
scheduleRefresh(Bridge *b)
{
    if (b->pending || b->dead)
        return;
    b->pending = true;
    QTimer::singleShot(0, b, [b]
    {
        b->pending = false;
        if (!b->dead)
            refreshAll(b);
    });
}

void
ensureConnected(Bridge *b)
{
    Statusbar *statusbar = b->lektra->statusbar();
    if (b->connected || !statusbar)
        return;
    b->connected = true;

    QObject::connect(statusbar, &Statusbar::changed, b,
                     [b] { scheduleRefresh(b); });
    QObject::connect(statusbar, &Statusbar::customModuleClicked, b,
                     [b](const QString &name)
    {
        const auto it = b->segments.constFind(name);
        if (b->dead || it == b->segments.constEnd()
            || it->onClick == LUA_NOREF)
            return;
        lua_State *L = b->L;
        lua_rawgeti(L, LUA_REGISTRYINDEX, it->onClick);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK)
        {
            fprintf(stderr, "Lua error in statusbar click: %s\n",
                    lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    });
}

// Reads opts.tooltip, opts.on_click (and opts.interval if `timer`) into the
// segment. `index` is the options table, if there is one.
void
readOptions(Bridge *b, const QString &name, Segment &segment, int index,
            bool timer)
{
    lua_State *L = b->L;
    if (!lua_istable(L, index))
        return;

    lua_getfield(L, index, "tooltip");
    if (lua_isstring(L, -1))
        segment.tooltip = QString::fromUtf8(lua_tostring(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, index, "on_click");
    if (lua_isfunction(L, -1))
        segment.onClick = luaL_ref(L, LUA_REGISTRYINDEX);
    else
        lua_pop(L, 1);

    if (timer)
    {
        lua_getfield(L, index, "interval");
        if (lua_isnumber(L, -1) && lua_tonumber(L, -1) > 0)
        {
            const int ms = std::max(100, static_cast<int>(lua_tonumber(L, -1) * 1000));
            segment.timer = new QTimer(b);
            segment.timer->setInterval(ms);
            QObject::connect(segment.timer, &QTimer::timeout, b,
                             [b, name] { evaluate(b, name); });
            segment.timer->start();
        }
        lua_pop(L, 1);
    }
}

// Both register() and set() leave a segment behind; this makes a fresh one.
Segment &
freshSegment(Bridge *b, const QString &name)
{
    auto it = b->segments.find(name);
    if (it != b->segments.end())
        dropRefs(b, *it);
    return b->segments[name];
}
} // namespace

void
Lektra::initLuaStatusbar() noexcept
{
    auto *bridge = new Bridge(this, m_L);

    lua_newtable(m_L);

    lua_pushlightuserdata(m_L, bridge);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        Bridge *b          = bridgeOf(L);
        const QString name = nameArg(L, 1);
        checkName(L, name);

        if (!lua_isfunction(L, 2) && !lua_isstring(L, 2))
            return luaL_error(L, "the second argument is a function or a string");

        Segment &segment = freshSegment(b, name);
        if (lua_isfunction(L, 2))
        {
            lua_pushvalue(L, 2);
            segment.provider = luaL_ref(L, LUA_REGISTRYINDEX);
        }
        else
        {
            segment.text = QString::fromUtf8(lua_tostring(L, 2));
        }
        readOptions(b, name, segment, 3, true);

        ensureConnected(b); // does nothing before the statusbar exists
        evaluate(b, name);
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "register");

    lua_pushlightuserdata(m_L, bridge);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        Bridge *b          = bridgeOf(L);
        const QString name = nameArg(L, 1);
        checkName(L, name);

        const QString text = lua_isstring(L, 2)
                                 ? QString::fromUtf8(lua_tostring(L, 2))
                                 : QString();

        Segment &segment = freshSegment(b, name);
        segment.text     = text;
        readOptions(b, name, segment, 3, false);

        // before the statusbar exists (init.lua runs first) it is shown later,
        // by Lektra::applyLuaStatusbar()
        if (b->lektra->statusbar())
        {
            ensureConnected(b);
            b->lektra->statusbar()->setCustomModule(
                name, segment.text, segment.tooltip, segment.onClick != LUA_NOREF);
        }
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "set");

    lua_pushlightuserdata(m_L, bridge);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        Bridge *b = bridgeOf(L);
        if (lua_isnoneornil(L, 1))
            refreshAll(b);
        else
            evaluate(b, nameArg(L, 1));
        return 0;
    }, 1);
    lua_setfield(m_L, -2, "update");

    lua_pushlightuserdata(m_L, bridge);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        Bridge *b          = bridgeOf(L);
        const QString name = nameArg(L, 1);
        auto it            = b->segments.find(name);
        if (it == b->segments.end())
        {
            lua_pushboolean(L, 0);
            return 1;
        }
        dropRefs(b, *it);
        b->segments.erase(it);
        if (b->lektra->statusbar())
            b->lektra->statusbar()->removeCustomModule(name);
        lua_pushboolean(L, 1);
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "unregister");

    lua_setfield(m_L, -2, "statusbar");
}

// The application is closing: no segment may call into Lua any more.
void
Lektra::killLuaStatusbar() noexcept
{
    auto *bridge = findChild<QObject *>(QStringLiteral("lektraStatusbar"));
    if (!bridge)
        return;
    auto *b = static_cast<Bridge *>(bridge);
    b->dead = true;
    b->disconnect();
    for (Segment &segment : b->segments)
        if (segment.timer)
            segment.timer->stop();
}

// The statusbar has just been made: shows the segments the scripts registered
// before that (init.lua runs first).
void
Lektra::applyLuaStatusbar() noexcept
{
    auto *bridge = findChild<QObject *>(QStringLiteral("lektraStatusbar"));
    if (!bridge)
        return;
    auto *b = static_cast<Bridge *>(bridge);
    ensureConnected(b);
    refreshAll(b);
}
