# Lektra

## Config options

When adding, removing or changing a config option, update all of these:

1. `include/Config.hpp`: the field plus its `@desc` / `@type` / `@default` / `@added` annotations.
   A multi-line `@desc` must use the `{ ... }` form.
2. `src/lektra/Config.cpp`: the TOML loader.
3. `src/lua/opt.cpp` and `stubs/lua/opt.lua`: the Lua option and its stub (both hand-written).
4. `CHANGELOG.md`: a short, user-facing entry.

Then run the generators. Never hand-edit their output.

    python3 scripts/gen_config_schema.py    # writes schema/lektra-config.schema.json
    python3 scripts/test_config_schema.py   # needs `jsonschema`
    python3 scripts/gen_docs_md.py          # writes docs/reference/ (not tracked)

Also do this after adding or changing a command (`reg(...)` description) or
editing the `---` comments in `stubs/lua/*.lua`: re-run `gen_docs_md.py`.
