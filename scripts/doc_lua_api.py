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
RE_FIELD     = re.compile(r'---@field\s+(\S+)\s+(.*)')
RE_PARAM     = re.compile(r'---@param\s+(\w+\??)\s+(.*)')
RE_RETURN    = re.compile(r'---@return\s+(.+)')
RE_DESC      = re.compile(r'---\s?(.*)')
RE_METHOD    = re.compile(r'function\s+(\w+):(\w+)\s*\(([^)]*)\)')
RE_MOD_FUNC  = re.compile(r'(lektra\.[\w.]+)\s*=\s*function\s*\(([^)]*)\)')
RE_MOD_INIT  = re.compile(r'lektra\.(\w+)\s*=\s*\{\}')
RE_LOCAL_CLS = re.compile(r'local\s+(\w+)\s*=\s*\{\}')


def split_type(text: str) -> tuple[str, str]:
    """Splits "type rest of the line" into the type and the rest.

    A type can contain spaces inside brackets, as in `table<string, number>`
    or `fun(a: string): boolean`, and around "|", as in `string | nil`.
    """
    text = text.strip()
    pairs = {"<": ">", "(": ")", "{": "}", "[": "]"}
    closers = set(pairs.values())
    depth = 0
    i = 0
    while i < len(text):
        c = text[i]
        if c in pairs:
            depth += 1
        elif c in closers:
            depth = max(0, depth - 1)
        elif c.isspace() and depth == 0:
            # a space around "|" belongs to the type, and so does the return
            # type of a function type, fun(a: string): boolean
            before = text[:i].rstrip()
            after = text[i:].lstrip()
            if before.endswith(("|", ":")) or after.startswith("|"):
                i += 1
                continue
            break
        i += 1
    return text[:i].strip(), text[i:].strip()


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

    module_names: list[str] = []
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
                ftype, fdesc = split_type(m.group(2))
                entry = {"name": m.group(1), "type": ftype, "desc": fdesc}
                classes.setdefault(pending_class_name, {"desc": "", "fields": [], "methods": []})
                classes[pending_class_name]["fields"].append(entry)
            continue

        # @param
        m = RE_PARAM.match(s)
        if m:
            ptype, pdesc = split_type(m.group(2))
            pending_params.append({"name": m.group(1), "type": ptype, "desc": pdesc})
            continue

        # @return
        m = RE_RETURN.match(s)
        if m:
            rtype, rest = split_type(m.group(1))
            parts = rest.split(None, 1)
            pending_return = {
                "type": rtype,
                "name": parts[0] if parts else "",
                "desc": parts[1] if len(parts) > 1 else "",
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
            module_names.append(f"lektra.{m.group(1)}")
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
        # a stub that declares several modules is listed under all of them
        "module": ", ".join(module_names) if module_names else None,
        "module_desc": "",
        "classes": class_list,
        "functions": functions,
    }


# Stub files to include and the display order. Stubs that are not listed here
# are added after these, in alphabetical order, so a new stub is never lost.
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
    "clipboard.lua",
    "sessions.lua",
    "timer.lua",
    "job.lua",
    "async.lua",
    "paths.lua",
    "statusbar.lua",
    "capabilities.lua",
    "version.lua",
]

# Manual module names for stubs that don't declare one
MANUAL_MODULE = {
    "lektra.lua": "lektra",
    "capabilities.lua": "lektra.capabilities",
    "async.lua": "lektra.async",
}


def collect_modules(stubs_dir: str) -> list[dict]:
    """Parses the stubs of a directory into a list of modules, in display order."""
    present = sorted(f for f in os.listdir(stubs_dir) if f.endswith(".lua"))
    names = [f for f in STUB_ORDER if f in present]
    names += [f for f in present if f not in STUB_ORDER]

    results = []
    for fname in names:
        mod = parse_stub(os.path.join(stubs_dir, fname))
        if mod["module"] is None:
            mod["module"] = MANUAL_MODULE.get(fname)
        if not mod["module"]:
            continue
        # skip completely empty modules
        if not mod["classes"] and not mod["functions"]:
            continue
        results.append(mod)
    return results


def main():
    default_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "stubs", "lua")
    stubs_dir = sys.argv[1] if len(sys.argv) > 1 else default_dir

    results = collect_modules(stubs_dir)

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
