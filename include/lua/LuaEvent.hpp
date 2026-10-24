#pragma once

#include "DispatchType.hpp"
#include "LuaCompat.hpp"
#include "Model.hpp"

// Pushes the table an event callback receives: { type = "OnLinkClicked",
// view = ..., <payload fields> }. `data` is what the event was dispatched
// with (a DocumentView for most view events).
void
pushLuaEvent(lua_State *L, DispatchType type, void *data);

// Pushes a link as a PageLink table without `index` and `rect`, with `page`
// from the info's source page.
void
pushLuaLink(lua_State *L, const Model::LinkInfo &info);

// Reads an event from the Lua argument at `idx`: its name ("OnPageChanged")
// or a `lektra.event.EventType` value. Raises a Lua error for an unknown one.
DispatchType
checkLuaDispatchType(lua_State *L, int idx);
