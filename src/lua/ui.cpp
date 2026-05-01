#include "Lektra.hpp"

#include <QMessageBox>

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
    lua_setfield(m_L, -2, "show_message");

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
        // Qt handles const char* nicely, but we ensure they are valid pointers
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

    lua_setfield(m_L, -2, "ui");
}
