#include "ColorDialog.hpp"
#include "Lektra.hpp"
#include "lua/LuaPicker.hpp"

#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>

namespace
{

// Helper: register the QMenu metatable (call once at Lua init)
static void
registerMenuMetatable(lua_State *L)
{
    if (luaL_newmetatable(L, "LektraMenu")) // only creates if not exists
    {
        // __index = method table
        lua_newtable(L);

        lua_pushcclosure(L, [](lua_State *L) -> int
        {
            if (!lua_isuserdata(L, 1))
                return 0;

            QMenu **ud = static_cast<QMenu **>(lua_touserdata(L, 1));
            if (!ud || !*ud)
                return 0;

            QMenu *menu = *ud;
            menu->popup(QCursor::pos());
            return 0;
        }, 0);
        lua_setfield(L, -2, "show");

        lua_pushcclosure(L, [](lua_State *L) -> int
        {
            if (!lua_isuserdata(L, 1))
                return 0;

            const char *label = luaL_checkstring(L, 2);

            QMenu **ud = static_cast<QMenu **>(lua_touserdata(L, 1));
            if (!ud || !*ud)
                return 0;

            QMenu *menu = *ud;
            QAction *action = menu->addAction(QString::fromUtf8(label));

            if (lua_isfunction(L, 3))
            {
                lua_pushvalue(L, 3);
                int callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
                QObject::connect(action, &QAction::triggered,
                                 [L, callback_ref]()
                {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, callback_ref);
                    if (lua_pcall(L, 0, 0, 0) != LUA_OK)
                    {
                        const char *errorMsg = lua_tostring(L, -1);
                        fprintf(stderr, "Lua menu callback error: %s\n",
                                errorMsg);
                        lua_pop(L, 1);
                    }
                });
                QObject::connect(action, &QObject::destroyed,
                                 [L, callback_ref]()
                { luaL_unref(L, LUA_REGISTRYINDEX, callback_ref); });
            }

            return 0;
        }, 0);
        lua_setfield(L, -2, "add_item");

        lua_setfield(L, -2, "__index"); // metatable.__index = method table
    }
    lua_pop(L, 1); // pop metatable
}

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

    // lektra.ui.file_dialog(mode, options)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        const char *mode    = "open";
        const char *def_dir = nullptr;
        const char *filters = nullptr;
        QString title       = nullptr;

        if (lua_istable(L, 1))
        {
            lua_getfield(L, 1, "mode");
            if (lua_isstring(L, -1))
                mode = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        else
        {
            mode = luaL_optstring(L, 1, "open");
        }

        if (lua_istable(L, 1))
        {
            lua_getfield(L, 1, "default_path");
            if (lua_isstring(L, -1))
                def_dir = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, 1, "filters");
            if (lua_isstring(L, -1))
                filters = lua_tostring(L, -1);
            lua_pop(L, 1);
        }

        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        QString selected_file;
        if (strcmp(mode, "open") == 0)
        {
            title         = tr("Open File");
            selected_file = QFileDialog::getOpenFileName(
                lektra, title, def_dir ? QString::fromUtf8(def_dir) : QString(),
                filters ? QString::fromUtf8(filters) : QString());
        }
        else if (strcmp(mode, "save") == 0)
        {
            title         = tr("Save File");
            selected_file = QFileDialog::getSaveFileName(
                lektra, title, def_dir ? QString::fromUtf8(def_dir) : QString(),
                filters ? QString::fromUtf8(filters) : QString());
        }
        else
        {
            return luaL_error(L, "Invalid file dialog mode: %s", mode);
        }

        if (!selected_file.isEmpty())
        {
            lua_pushstring(L, selected_file.toUtf8().constData());
            return 1;
        }

        return 0;
    }, 1);
    lua_setfield(m_L, -2, "file_dialog");

    // lektra.ui.color_dialog(colors: string[], default_color: string)
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        if (!lua_istable(L, 1))
            return luaL_error(L, "Expected table of colors");

        std::vector<QColor> colors;
        lua_pushnil(L);
        while (lua_next(L, 1))
        {
            if (lua_isstring(L, -1))
            {
                QColor color(lua_tostring(L, -1));
                if (color.isValid())
                    colors.push_back(color);
            }
            lua_pop(L, 1);
        }

        QColor default_color = colors.at(0);

        ColorDialog dlg(colors, default_color, lektra);
        dlg.exec();

        if (dlg.selectedColor().isValid())
        {
            lua_pushstring(
                L,
                dlg.selectedColor().name(QColor::HexArgb).toUtf8().constData());
            return 1;
        }

        return 0;
    }, 1);
    lua_setfield(m_L, -2, "color_dialog");

    registerMenuMetatable(m_L);

    // menu_items: MenuItem[]
    // MenuItem = { label: string, callback: function, submenu?: MenuItem[],
    // icon?: string} lektra.ui.menu(menu_items) -> Menu
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        if (!lua_istable(L, 1))
        {
            lua_pushnil(L);
            return 1;
        }

        QMenu *menu = new QMenu(lektra);

        std::function<void(int, QMenu *)> parseMenuItems
            = [&](int tbl, QMenu *parent_menu)
        {
            lua_pushnil(L);
            while (lua_next(L, tbl))
            {
                if (!lua_istable(L, -1))
                {
                    lua_pop(L, 1);
                    continue;
                }

                lua_getfield(L, -1, "label");
                if (!lua_isstring(L, -1))
                {
                    lua_pop(L, 2);
                    continue;
                }
                QString label = QString::fromUtf8(lua_tostring(L, -1));
                lua_pop(L, 1);

                QAction *action = parent_menu->addAction(label);

                lua_getfield(L, -1, "submenu");
                if (lua_istable(L, -1))
                {
                    QMenu *submenu = new QMenu(label, parent_menu);
                    action->setMenu(submenu);
                    parseMenuItems(lua_gettop(L), submenu);
                }
                lua_pop(L, 1);

                lua_getfield(L, -1, "icon");
                if (lua_isstring(L, -1))
                {
                    QString icon_path = QString::fromUtf8(lua_tostring(L, -1));
                    QIcon icon        = QIcon::fromTheme(icon_path);
                    if (!icon.isNull())
                        action->setIcon(icon);
                }
                lua_pop(L, 1);

                lua_getfield(L, -1, "callback");
                if (lua_isfunction(L, -1))
                {
                    int callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
                    QObject::connect(action, &QAction::triggered,
                                     [L, callback_ref]()
                    {
                        lua_rawgeti(L, LUA_REGISTRYINDEX, callback_ref);
                        lua_call(L, 0, 0);
                    });
                    QObject::connect(action, &QObject::destroyed,
                                     [L, callback_ref]()
                    { luaL_unref(L, LUA_REGISTRYINDEX, callback_ref); });
                }
                else
                {
                    lua_pop(L, 1);
                }

                lua_pop(L, 1); // pop item table
            }
        };

        parseMenuItems(1, menu);

        // Push as full userdata so we can attach a metatable
        QMenu **ud = static_cast<QMenu **>(lua_newuserdata(L, sizeof(QMenu *)));
        *ud        = menu;
        luaL_setmetatable(L, "LektraMenu");
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "menu");

    lua_setfield(m_L, -2, "ui");
}
