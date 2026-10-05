# Example config.toml

A `config.toml` that sets a few of the most common options. Put it in the config
folder, `~/.config/lektra/` (`%APPDATA%\lektra\` on Windows), and change what you
like. Everything is optional: a section or a key you leave out keeps its default.

```toml
--8<-- "docs/example_config.toml"
```

## Things to know

- **Every option** is described in the [TOML configuration](reference/toml_configuration.md)
  reference, and [`default_config.toml`](https://codeberg.org/lektra/lektra/src/branch/main/default_config.toml)
  lists them all with their defaults.
- **Key bindings** are added to the defaults while `load_defaults = true`. Set it
  to `false` to start from an empty keymap and list only the keys you want. A value
  is a key (`"Ctrl+d"`), a list of keys, or a chord with a comma (`"g,g"`). The
  names on the left are [commands](reference/commands.md).
- **Per file type:** `[filetype.<type>]` changes view settings for one kind of
  document only, here EPUB and DjVu. The type is lower case (`pdf`, `epub`, ...).
- **Editor help:** the `#:schema` line on top gives completion and warnings in an
  editor with a TOML language server, such as [Taplo](https://taplo.tamasfe.dev/).
- **Try it without installing it:** `lektra --config example_config.toml file.pdf`
  loads just that file.
- **The same in Lua:** the options are also available as `lektra.opt.<section>.<option>`
  in `init.lua`, for example:

```lua
lektra.opt.zoom.level = 1.0
lektra.opt.behavior.confirm_on_quit = true
```
