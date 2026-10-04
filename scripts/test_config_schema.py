#!/usr/bin/env python3
"""Checks for the generated config.toml JSON Schema.

    scripts/test_config_schema.py

Needs the `jsonschema` package. Exits non-zero on any failure.
"""
import json
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import gen_config_schema as gen  # noqa: E402

try:
    import jsonschema
except ImportError:
    sys.exit("test_config_schema.py needs the `jsonschema` package")

failures = []


def check(name, ok, detail=""):
    print(("ok   " if ok else "FAIL ") + name + (f"  {detail}" if detail and not ok else ""))
    if not ok:
        failures.append(name)


fresh = gen.Builder().build()
committed = json.loads(gen.OUT_DEFAULT.read_text())
check("committed schema is up to date (run scripts/gen_config_schema.py)", fresh == committed)

jsonschema.Draft7Validator.check_schema(fresh)
validator = jsonschema.Draft7Validator(fresh)


def errors(text):
    return [f"{'.'.join(map(str, e.absolute_path))}: {e.message}"
            for e in validator.iter_errors(tomllib.loads(text))]


check("default_config.toml is valid", not errors(gen.DEFAULT_TOML.read_text()))

VALID = {
    "empty file": "",
    "layout and zoom": '[layout]\nmode="single"\nspacing=12\n[zoom]\nlevel=0.8\n',
    "colours": '[page]\nbg="#101010"\nfg="#FFFFFF80"\n',
    "filetype, dotted keys": '[filetype.epub]\nlayout.mode="single"\nreflow.font_size=14\n',
    "filetype, tables": '[filetype.pdf.behavior]\ndont_invert_images=true\n',
    "keybindings": '[keybindings]\nload_defaults=false\nzoom_in=["="]\nzoom_out="-"\n',
    "mousebindings": '[mousebindings]\npan="Alt+LeftButton"\n',
    "statusbar": '[statusbar]\npadding=[4,2,4,2]\n[statusbar.components.zoom]\nshow=false\n',
    "dpr as a number": '[rendering]\ndpr=1.5\n',
    "dpr per screen": '[rendering.dpr]\n"eDP-1"=1.5\n',
    "picker keys": '[picker.keys]\naccept="Return"\nup=["Up","Ctrl+k"]\n',
    "llm view": '[llm_view]\nmodel="llama3"\n[llm_view.extra_body]\nstream=true\n',
}
for name, text in VALID.items():
    e = errors(text)
    check(f"accepts: {name}", not e, "; ".join(e))

INVALID = {
    "unknown section": ('[layouts]\nmode="single"\n', "layouts"),
    "unknown key": ('[layout]\nmodee="single"\n', "modee"),
    "wrong type": ('[zoom]\nlevel="big"\n', "zoom.level"),
    "value not in the choices": ('[layout]\nmode="diagonal"\n', "layout.mode"),
    "bad colour": ('[page]\nbg="red"\n', "page.bg"),
    "section written as a value": ('[filetype.epub]\nlayout="single"\n', "filetype.epub.layout"),
    "section not allowed in filetype": ('[filetype.epub.statusbar]\nvisible=false\n', "statusbar"),
    "string where boolean expected": ('[behavior]\ninvert_mode="yes"\n', "behavior.invert_mode"),
    "keybinding of the wrong type": ('[keybindings]\nzoom_in=5\n', "keybindings.zoom_in"),
}
for name, (text, where) in INVALID.items():
    e = errors(text)
    check(f"rejects: {name}", any(where in x for x in e), "not flagged" if not e else "; ".join(e))

# every option has a description (hover text in the editor)
missing = []


def walk(node, path):
    for k, v in node.get("properties", {}).items():
        leaf = v.get("type") != "object" or not v.get("properties")
        if leaf and not v.get("description"):
            missing.append(".".join(path + [k]))
        walk(v, path + [k])


walk(fresh, [])
check("every option has a description", not missing, ", ".join(missing))

print(f"\n{len(failures)} failed" if failures else "\nall passed")
sys.exit(1 if failures else 0)
