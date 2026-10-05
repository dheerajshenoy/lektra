# Examples

Lua scripts go in `init.lua` in the Lektra config folder (`lektra.paths.config()`
tells where it is). See the [Lua API](reference/lua_api.md) and the
[scripting guide](LUA-WIKI.md) for what is available.

## An `init.lua`

```lua
--8<-- "docs/example_init.lua"
```

## Small plugins

Commands that use background jobs (`lektra.async`), annotations, custom
statusbar segments and view syncing:

```lua
--8<-- "docs/example_plugins.lua"
```
