"""
MkDocs hooks (see `hooks:` in mkdocs.yml): keep docs/reference/ up to date.

The reference pages are generated from the sources by gen_docs_md.py. With this
hook that happens by itself:

  * before every build, so `mkdocs build` works on a fresh checkout (this is what
    Read the Docs runs), and
  * in `mkdocs serve`, whenever a source the pages are made from changes, so the
    page in the browser follows an edit of Config.hpp, a command or a Lua stub.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import gen_docs_md  # noqa: E402

# what the generated pages are made from
WATCHED = [
    ROOT / "include" / "Config.hpp",
    ROOT / "src",
    ROOT / "stubs" / "lua",
    ROOT / "scripts" / "gen_docs_md.py",
    ROOT / "scripts" / "doc_commands.py",
    ROOT / "scripts" / "doc_config.py",
    ROOT / "scripts" / "doc_lua_api.py",
    ROOT / "CMakeLists.txt",  # the version
]


def on_pre_build(config, **kwargs):
    gen_docs_md.generate(Path(config["docs_dir"]) / "reference")


def on_serve(server, config, builder, **kwargs):
    for path in WATCHED:
        server.watch(str(path))
    return server
