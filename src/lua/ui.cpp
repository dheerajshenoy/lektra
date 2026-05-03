#include "Lektra.hpp"
#include "lua/LuaPicker.hpp"

#include <QMessageBox>

namespace
{

// lektra.ui.picker(prompt, items, options)
static int
lua_ui_picker(lua_State *L, Lektra *lektra)
{
    // --- normalize: table-invoke vs positional ---
    QString prompt;
    int items_tbl   = 0;
    int options_tbl = 0;

    if (lua_istable(L, 1) && !lua_istable(L, 2))
    {
        // table-invoke: single table with all keys
        int cfg = 1;

        lua_getfield(L, cfg, "prompt");
        prompt = QString::fromUtf8(luaL_optstring(L, -1, ""));
        lua_pop(L, 1);

        lua_getfield(L, cfg, "items");
        if (!lua_istable(L, -1))
            return luaL_error(L, "picker: 'items' must be a table");
        items_tbl = lua_gettop(L); // absolute index, stays valid

        // options live in the same table — push a proxy ref
        options_tbl
            = cfg; // read flat/columns/on_accept/on_cancel from cfg directly
    }
    else
    {
        // positional: picker(prompt, items [, options])
        prompt = QString::fromUtf8(luaL_checkstring(L, 1));
        luaL_checktype(L, 2, LUA_TTABLE);
        items_tbl   = 2;
        options_tbl = lua_istable(L, 3) ? 3 : 0;
    }

    // --- parse items recursively (unchanged, uses items_tbl) ---
    std::function<QList<LuaPicker::LuaItem>(int)> parseItems
        = [&](int tbl) -> QList<LuaPicker::LuaItem>
    {
        QList<LuaPicker::LuaItem> result;
        lua_pushnil(L);
        while (lua_next(L, tbl))
        {
            LuaPicker::LuaItem item;
            int item_tbl = lua_gettop(L);

            if (lua_isstring(L, item_tbl))
            {
                item.columns = {QString::fromUtf8(lua_tostring(L, item_tbl))};
            }
            else if (lua_istable(L, item_tbl))
            {
                for (int col = 1;; ++col)
                {
                    lua_rawgeti(L, item_tbl, col);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    item.columns << QString::fromUtf8(lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
                lua_getfield(L, item_tbl, "children");
                if (lua_istable(L, -1))
                    item.children = parseItems(lua_gettop(L));
                lua_pop(L, 1);
            }

            result.append(item);
            lua_pop(L, 1);
        }
        return result;
    };

    auto items = parseItems(items_tbl);

    // --- parse options (reads from options_tbl, 0 = none) ---
    QStringList col_headers;
    bool flat         = false;
    int on_accept_ref = LUA_NOREF;
    int on_cancel_ref = LUA_NOREF;

    if (options_tbl > 0)
    {
        lua_getfield(L, options_tbl, "flat");
        if (lua_isboolean(L, -1))
            flat = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, options_tbl, "columns");
        if (lua_istable(L, -1))
        {
            int col_tbl = lua_gettop(L);
            lua_pushnil(L);
            while (lua_next(L, col_tbl))
            {
                col_headers << QString::fromUtf8(lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, options_tbl, "on_accept");
        if (lua_isfunction(L, -1))
            on_accept_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        else
            lua_pop(L, 1);

        lua_getfield(L, options_tbl, "on_cancel");
        if (lua_isfunction(L, -1))
            on_cancel_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        else
            lua_pop(L, 1);
    }

    QList<Picker::Column> col_defs;
    if (col_headers.isEmpty())
        col_defs.push_back({.header = "Value", .stretch = 1});
    else
        for (int i = 0; i < col_headers.size(); ++i)
            col_defs.push_back(
                {.header = col_headers[i], .stretch = (i == 0) ? 1 : 0});

    Config::Picker config;
    auto *picker = new LuaPicker(config, lektra);
    picker->setColumns(col_defs);
    picker->setStructureMode(flat ? Picker::StructureMode::Flat
                                  : Picker::StructureMode::Hierarchical);
    picker->setItems(items);

    if (on_accept_ref != LUA_NOREF)
        QObject::connect(picker, &LuaPicker::itemAccepted,
                         [L, on_accept_ref](const QString &text)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, on_accept_ref);
            lua_pushstring(L, text.toUtf8().constData());
            lua_call(L, 1, 0);
            luaL_unref(L, LUA_REGISTRYINDEX, on_accept_ref);
        });

    if (on_cancel_ref != LUA_NOREF)
        QObject::connect(picker, &QObject::destroyed, [L, on_cancel_ref]()
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, on_cancel_ref);
            lua_call(L, 0, 0);
            luaL_unref(L, LUA_REGISTRYINDEX, on_cancel_ref);
        });

    picker->setPrompt(prompt);
    picker->launch();
    return 0;
}
} // namespace

void
Lektra::initLuaUI() noexcept
{
    lua_newtable(m_L); // lektra.ui table

    // lektra.ui.show_message(title, message, type)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        // Provide safe defaults to avoid nullptr dereference
        const char *title   = "Notification";
        const char *message = "";
        const char *type    = "info";

        if (lua_istable(L, 1))
        {
            lua_getfield(L, 1, "title");
            if (lua_isstring(L, -1))
                title = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, 1, "message");
            if (lua_isstring(L, -1))
                message = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, 1, "type");
            if (lua_isstring(L, -1))
                type = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        else
        {
            title   = luaL_checkstring(L, 1);
            message = luaL_checkstring(L, 2);
            type    = luaL_optstring(L, 3, "info");
        }

        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        // Safe now because 'type' is guaranteed to be non-null
        if (strcmp(type, "info") == 0)
            QMessageBox::information(lektra, title, message);
        else if (strcmp(type, "warning") == 0)
            QMessageBox::warning(lektra, title, message);
        else if (strcmp(type, "error") == 0)
            QMessageBox::critical(lektra, title, message);
        else
            return luaL_error(L, "Invalid message type: %s", type);

        return 0;
    }, 1);
    lua_setfield(m_L, -2, "messagebox");

    // lektra.ui.message(message, duration)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        const char *message = "Message";
        float duration      = 2.0;

        if (lua_istable(L, 1))
        {
            lua_getfield(L, 1, "message");
            if (lua_isstring(L, -1))
                message = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, 1, "duration");
            if (lua_isnumber(L, -1))
                duration = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
        else
        {
            message  = luaL_checkstring(L, 1);
            duration = static_cast<float>(luaL_optnumber(L, 2, 2.0));
        }

        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        lektra->showMessage(QString::fromUtf8(message), duration);

        return 0;
    }, 1);
    lua_setfield(m_L, -2, "message");

    // lektra.ui.input(title, prompt)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        const char *title  = "Input";
        const char *prompt = "Enter value:";

        if (lua_istable(L, 1))
        {
            lua_getfield(L, 1, "title");
            if (lua_isstring(L, -1))
                title = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, 1, "prompt");
            if (lua_isstring(L, -1))
                prompt = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        else
        {
            title  = luaL_checkstring(L, 1);
            prompt = luaL_checkstring(L, 2);
        }

        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        bool ok      = false;
        // Qt handles const char* nicely, but we ensure they are valid
        // pointers
        QString text = QInputDialog::getText(lektra, title, prompt,
                                             QLineEdit::Normal, QString(), &ok);
        if (ok)
        {
            lua_pushstring(L, text.toUtf8().constData());
            return 1;
        }

        return 0;
    }, 1);
    lua_setfield(m_L, -2, "input");

    // lektra.ui.picker(prompt, items, [options])
    // Items can be heirarchical specified as a table of tables, can be
    // strings or numbers, but will be converted to strings for display
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));
        return lua_ui_picker(L, lektra);
    }, 1);
    lua_setfield(m_L, -2, "picker");

    lua_setfield(m_L, -2, "ui");
}
