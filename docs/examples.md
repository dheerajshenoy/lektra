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
\n
## Editor completion

Lektra installs type stubs that describe the whole `lektra` API. With the
[Lua language server](https://luals.github.io/) your editor then completes
`lektra.` functions and shows their documentation while you edit `init.lua`.

| System | Stubs folder |
|---|---|
| Linux (packages) | `/usr/share/lektra/lua/` |
| Windows | `share\lektra\lua` in the install folder, by default `C:\Program Files\Lektra\share\lektra\lua` |
| Built from source | `<prefix>/share/lektra/lua/`; the prefix is `/usr` by default and `/usr/local` with a plain `cmake --install` |

The same files are in the `stubs/lua/` folder of the repository. Point the
language server at the folder with a `.luarc.json` next to your `init.lua`:

```json
{
    "workspace": {
        "library": ["/usr/share/lektra/lua"]
    }
}
```
