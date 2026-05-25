#!/usr/bin/env python3
"""
Parse LuaLS stub files in stubs/lua/ and emit lua_api.json for the website.

Usage: python doc_lua_api.py [stubs/lua/dir]
"""

import json
import os
import re
import sys

HOME = os.getenv("HOME")

# ── Patterns ──────────────────────────────────────────────────────────────────
RE_CLASS     = re.compile(r'---@class\s+([\w.]+)')
RE_FIELD     = re.compile(r'---@field\s+(\S+)\s+(\S+)\s*(.*)')
RE_PARAM     = re.compile(r'---@param\s+(\w+\??)\s+(\S+)\s*(.*)')
RE_RETURN    = re.compile(r'---@return\s+(.+)')
RE_DESC      = re.compile(r'---\s?(.*)')
RE_METHOD    = re.compile(r'function\s+(\w+):(\w+)\s*\(([^)]*)\)')
RE_MOD_FUNC  = re.compile(r'(lektra\.[\w.]+)\s*=\s*function\s*\(([^)]*)\)')
RE_MOD_INIT  = re.compile(r'lektra\.(\w+)\s*=\s*\{\}')
RE_LOCAL_CLS = re.compile(r'local\s+(\w+)\s*=\s*\{\}')


def format_ret(ret: dict | None) -> str:
    if not ret:
        return ""
    parts = [ret["type"]]
    if ret.get("name"):
        parts.append(ret["name"])
    if ret.get("desc"):
        parts.extend(["—", ret["desc"]])
    return " ".join(parts)


def parse_stub(path: str) -> dict:
    with open(path) as f:
        lines = f.readlines()

    module_name = None
    classes: dict = {}   # cls_name -> {desc, fields, methods}
    functions: list = []

    # Pending doc block
    pending_desc: list[str] = []
    pending_params: list[dict] = []
    pending_return: dict | None = None
    pending_class_name: str | None = None    # class being annotated
    pending_class_desc: list[str] = []

    def reset():
        nonlocal pending_desc, pending_params, pending_return
        pending_desc = []
        pending_params = []
        pending_return = None

    def reset_class():
        nonlocal pending_class_name, pending_class_desc
        pending_class_name = None
        pending_class_desc = []

    def take():
        desc = " ".join(pending_desc).strip()
        if desc.startswith("[") and desc.endswith("]"):
            desc = desc[1:-1].strip()
        params = pending_params[:]
        ret = pending_return
        reset()
        return desc, params, ret

    def make_entry(name, sig, desc, params, ret):
        e = {"name": name, "sig": sig, "desc": desc}
        if params:
            e["params"] = params
        r = format_ret(ret)
        if r:
            e["returns"] = r
        return e

    for raw in lines:
        s = raw.strip()

        # @class
        m = RE_CLASS.match(s)
        if m:
            reset()
            reset_class()
            pending_class_name = m.group(1)
            pending_class_desc = []
            continue

        # @field
        m = RE_FIELD.match(s)
        if m:
            if pending_class_name:
                entry = {"name": m.group(1), "type": m.group(2), "desc": m.group(3).strip()}
                classes.setdefault(pending_class_name, {"desc": "", "fields": [], "methods": []})
                classes[pending_class_name]["fields"].append(entry)
            continue

        # @param
        m = RE_PARAM.match(s)
        if m:
            pending_params.append({"name": m.group(1), "type": m.group(2), "desc": m.group(3).strip()})
            continue

        # @return
        m = RE_RETURN.match(s)
        if m:
            parts = m.group(1).strip().split(None, 2)
            pending_return = {
                "type": parts[0],
                "name": parts[1] if len(parts) > 1 else "",
                "desc": parts[2] if len(parts) > 2 else "",
            }
            continue

        # skip all other @-tags (overload, meta, enum, etc.)
        if s.startswith("---@"):
            continue

        # description line (--- text)
        m = RE_DESC.match(s)
        if m:
            text = m.group(1).strip()
            if pending_class_name and not pending_params and not pending_return:
                # before any @field/@param, this is the class description
                pending_class_desc.append(text)
            else:
                pending_desc.append(text)
            continue

        # lektra.X = {} → module init
        m = RE_MOD_INIT.match(s)
        if m:
            module_name = f"lektra.{m.group(1)}"
            reset(); reset_class()
            continue

        # local X = {} → close pending class definition
        m = RE_LOCAL_CLS.match(s)
        if m:
            if pending_class_name:
                classes.setdefault(pending_class_name, {"desc": "", "fields": [], "methods": []})
                classes[pending_class_name]["desc"] = " ".join(pending_class_desc).strip()
                reset_class()
            reset()
            continue

        # function ClassName:method(params) end
        m = RE_METHOD.match(s)
        if m:
            cls_name, method_name, params_str = m.group(1), m.group(2), m.group(3)
            desc, params, ret = take()
            sig = f"{cls_name}:{method_name}({params_str})"
            entry = make_entry(method_name, sig, desc, params, ret)
            classes.setdefault(cls_name, {"desc": "", "fields": [], "methods": []})
            classes[cls_name]["methods"].append(entry)
            reset_class()
            continue

        # lektra.module.func = function(params)
        m = RE_MOD_FUNC.match(s)
        if m:
            full_name, params_str = m.group(1), m.group(2)
            desc, params, ret = take()
            name = full_name.split(".")[-1]
            sig = f"{full_name}({params_str})"
            entry = make_entry(name, sig, desc, params, ret)
            functions.append(entry)
            reset_class()
            continue

        # anything else (blank lines, non-comment code) → reset pending
        if not s.startswith("---"):
            reset()

    # Build ordered class list — skip classes with no methods and no fields
    class_list = [
        {"name": k, "desc": v["desc"], "fields": v["fields"], "methods": v["methods"]}
        for k, v in classes.items()
        if v["methods"] or v["fields"]
    ]

    return {
        "module": module_name,
        "module_desc": "",
        "classes": class_list,
        "functions": functions,
    }


# Stub files to include and the display order
STUB_ORDER = [
    "lektra.lua",
    "view.lua",
    "tabs.lua",
    "cmd.lua",
    "keymap.lua",
    "mousemap.lua",
    "event.lua",
    "bookmark.lua",
    "ui.lua",
    "opt.lua",
    "utils.lua",
    "capabilities.lua",
    "version.lua",
]

# Manual module names for stubs that don't declare one
MANUAL_MODULE = {
    "lektra.lua": "lektra",
    "capabilities.lua": "lektra.capabilities",
}


def main():
    stubs_dir = sys.argv[1] if len(sys.argv) > 1 else "../stubs/lua"

    results = []
    for fname in STUB_ORDER:
        path = os.path.join(stubs_dir, fname)
        if not os.path.isfile(path):
            continue
        mod = parse_stub(path)
        if mod["module"] is None:
            mod["module"] = MANUAL_MODULE.get(fname)
        if not mod["module"]:
            continue
        # skip completely empty modules
        if not mod["classes"] and not mod["functions"]:
            continue
        results.append(mod)

    out_path = f"{HOME}/Gits/dheerajshenoy.github.io/lektra/files/lua_api.json"
    with open(out_path, "w") as f:
        json.dump(results, f, indent=2)

    total_funcs = sum(
        len(m["functions"]) + sum(len(c["methods"]) for c in m["classes"])
        for m in results
    )
    print(f"Wrote {len(results)} modules, {total_funcs} total entries → {out_path}")


if __name__ == "__main__":
    main()
