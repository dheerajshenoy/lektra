#!/usr/bin/env python3
"""Generate a JSON Schema for Lektra's config.toml.

The schema lets a TOML language server (Taplo) offer completion, hover
documentation and validation while editing the config file.

Where the information comes from, so it cannot drift from the code:

  * src/lektra/ConfigOptions.cpp
                            the option list: sections, keys, value kinds,
                            string choices and per file type scope
  * include/Config.hpp      C++ member types and defaults, plus the
                            `// @desc / @type / @default / @choice`
                            annotations (read with scripts/doc_config.py)

Options of Kind::Custom (free-form shapes such as [rendering].dpr) and
[keybindings] are described in SPECIAL / CUSTOM_KIND below.

Usage:
    scripts/gen_config_schema.py                    write schema/lektra-config.schema.json
    scripts/gen_config_schema.py --stdout           print instead
    scripts/gen_config_schema.py --check            also validate default_config.toml
                                                    (needs the `jsonschema` package)
"""

import argparse
import collections
import json
import re
import sys
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))

from doc_config import Parser  # noqa: E402  (reads the @desc/@type annotations)

CONFIG_HPP = ROOT / "include" / "Config.hpp"
OPTIONS_CPP = ROOT / "src" / "lektra" / "ConfigOptions.cpp"
DEFAULT_TOML = ROOT / "default_config.toml"
OUT_DEFAULT = ROOT / "schema" / "lektra-config.schema.json"

COLOR_PATTERN = r"^#?([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$"


def strip_comments(line):
    """Drop a trailing // comment, but not a // inside a string literal."""
    if 'R"' in line:
        return line                      # raw string: leave the line alone
    in_str = False
    i = 0
    while i < len(line):
        c = line[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "/" and line.startswith("//", i):
            return line[:i]
        i += 1
    return line


# ---------------------------------------------------------------------------
# Config.hpp: struct tree (member types and defaults)
# ---------------------------------------------------------------------------

class Frame:
    def __init__(self, name, kind="struct", bases=()):
        self.name = name
        self.kind = kind            # struct | block
        self.bases = list(bases)
        self.members = {}           # member name -> (cpp type, default text)
        self.children = {}          # instance name -> Frame


def parse_structs(path):
    root = Frame("Config")
    stack = [root]
    by_name = {}
    pending = None  # struct opener whose '{' is on a later line
    root_brace_pending = False

    for raw in path.read_text().splitlines():
        line = strip_comments(raw).strip()
        if not line:
            continue

        m = re.match(r"struct\s+(\w+)\s*(?::\s*(?:public\s+)?([\w:]+))?\s*(\{)?\s*$", line)
        if m and not line.endswith(";"):
            # the file's own `struct Config {` is the root frame
            frame = root if (m.group(1) == "Config" and len(stack) == 1) else \
                Frame(m.group(1), bases=[m.group(2)] if m.group(2) else [])
            if frame is root:
                root_brace_pending = not m.group(3)
                continue
            if m.group(3):
                stack.append(frame)
            else:
                pending = frame
            continue
        if line == "{" and root_brace_pending:
            root_brace_pending = False    # opening brace of `struct Config`
            continue
        if pending is not None and line == "{":
            stack.append(pending)
            pending = None
            continue

        if line.startswith("#"):
            continue                      # preprocessor line

        net = line.count("{") - line.count("}")
        if line.startswith("}"):
            if len(stack) > 1:
                frame = stack.pop()
                inst = re.match(r"\}\s*(\w+)\s*;", line)
                if frame.kind == "struct":
                    by_name[frame.name] = frame
                    if inst:
                        stack[-1].children[inst.group(1)] = frame
            net += 1                      # the leading brace is handled above
        if net > 0:
            # enum, function body, initialiser list...: not a struct
            for _ in range(net):
                stack.append(Frame("", kind="block"))
            continue
        if net < 0:
            for _ in range(-net):
                if len(stack) > 1:
                    stack.pop()
            continue
        if line.startswith("}"):
            continue

        if stack[-1].kind != "struct":
            continue
        m = re.match(
            r"(?:static\s+|constexpr\s+|inline\s+)*([A-Za-z_][\w:<>,\*&\s]*?)\s+(\w+)\s*(?:=\s*(.+?))?;\s*$",
            line,
        )
        if m and "(" not in m.group(1):
            stack[-1].members[m.group(2)] = (m.group(1).strip(), m.group(3))

    # inherit members from base structs (e.g. Outline : Picker)
    def inherit(frame):
        for base in frame.bases:
            b = by_name.get(base.split("::")[-1])
            if b:
                inherit(b)
                for k, v in b.members.items():
                    frame.members.setdefault(k, v)
                for k, v in b.children.items():
                    frame.children.setdefault(k, v)
        for c in frame.children.values():
            inherit(c)

    inherit(root)
    return root, by_name


def parse_enums(path):
    """enum class members of Config.hpp, as the lower_snake names used in the file."""
    text = re.sub(r"//[^\n]*", "", path.read_text())
    out = {}
    for m in re.finditer(r"enum\s+(?:class\s+)?(\w+)\s*(?::\s*\w+\s*)?\{([^}]*)\}", text):
        names = [n.strip().split("=")[0].strip() for n in m.group(2).split(",") if n.strip()]
        out[m.group(1)] = [re.sub(r"(?<!^)(?=[A-Z])", "_", n).lower() for n in names]
    return out


def resolve_member(root, dotted):
    """Config member type for e.g. 'statusbar.component.mode.show'."""
    parts = dotted.split(".")
    frame = root
    for p in parts[:-1]:
        frame = frame.children.get(p)
        if frame is None:
            return None
    return frame.members.get(parts[-1])


CPP_TYPES = {
    "bool": "boolean",
    "int": "integer", "unsigned": "integer", "unsigned int": "integer",
    "long": "integer", "qint64": "integer", "size_t": "integer",
    "int64_t": "integer", "uint32_t": "integer",
    "float": "number", "double": "number", "qreal": "number",
    "QString": "string", "std::string": "string",
}


def json_type_for_cpp(cpp_type):
    t = cpp_type.replace("const ", "").strip()
    if t in CPP_TYPES:
        return CPP_TYPES[t]
    if t.startswith("std::array") or t.startswith("QList") or t.startswith("QStringList") \
            or t.startswith("std::vector"):
        return "array"
    if t.startswith("QVariantMap") or t.startswith("QMap"):
        return "object"
    return "string"   # enums are written as strings in the config


def cpp_default_to_json(text):
    if text is None:
        return None
    t = text.strip().rstrip(";")
    if t in ("true", "false"):
        return t == "true"
    m = re.fullmatch(r"(-?\d+(?:\.\d+)?)[fF]?", t)
    if m:
        v = m.group(1)
        return float(v) if "." in v else int(v)
    m = re.fullmatch(r'"(.*)"', t)
    if m:
        return m.group(1)
    return None


# ---------------------------------------------------------------------------
# ConfigOptions.cpp: the option list
# ---------------------------------------------------------------------------

class Node:
    def __init__(self):
        self.keys: "collections.OrderedDict[str, dict[str, Any]]" = collections.OrderedDict()  # key -> info
        self.children = collections.OrderedDict()  # sub-table -> Node
        self.scope = set()                          # scope names of its options

    def child(self, name):
        if name not in self.children:
            self.children[name] = Node()
        return self.children[name]

    def key(self, name):
        if name not in self.keys:
            self.keys[name] = {"kind": None, "target": None, "choices": []}
        return self.keys[name]


# Kind::Custom options that are plain values in config.toml (the others are in SPECIAL)
CUSTOM_KIND = {
    "window.title_format": "string",
    "presentation.layout": "value:std::string",
}

ENTRY = re.compile(r'r\.(boolean|integer|number|text|color|choice|custom)\(\s*"([\w.]+)"'
                   r'(?:\s*,\s*[FS]\(([\w.]+)\))?(?:\s*,\s*(\w+))?')
SECTION = re.compile(r'r\.section\(\s*"([\w.]+)"\s*,\s*([\w|\s]+?)\s*[,)]')
HELPER_CALL = re.compile(r'^\s*(\w+)\(r,\s*F\(([\w.]+)\)\)')


def extract_registry(path):
    text = path.read_text()
    code = [strip_comments(l) for l in text.splitlines()]

    choices = {m.group(1): re.findall(r'\{"(\w+)"', m.group(2))
               for m in re.finditer(r"constexpr Choice (\w+)\[\] = \{(.*?)\};", text, re.S)}

    # template <typename Base> void name(Registry &r, Base) { r.xxx("key", S(member)); ... }
    helpers = {}
    for m in re.finditer(r"template <typename Base>\s*void\s*(\w+)\(Registry &r, Base\)\s*\{(.*?)\n\}", text, re.S):
        helpers[m.group(1)] = [e.groups() for e in ENTRY.finditer(m.group(2))]

    root = Node()
    prefix, scope = None, ""
    entries = []                       # (dotted path, method, member, choices, scope)
    body = text[text.index("std::vector<Option>\nbuild()"):]
    for line in [strip_comments(l) for l in body.splitlines()]:
        m = SECTION.search(line)
        if m:
            prefix, scope = m.group(1), m.group(2)
            continue
        m = HELPER_CALL.match(line)
        if m and m.group(1) in helpers:
            for method, key, member, arr in helpers[m.group(1)]:
                entries.append((f"{prefix}.{key}", method, f"{m.group(2)}.{member}", arr, scope))
            continue
        m = ENTRY.search(line)
        if m:
            method, key, member, arr = m.groups()
            entries.append((f"{prefix}.{key}", method, member, arr, scope))

    for dotted, method, member, arr, sc in entries:
        parts = dotted.split(".")
        node = root
        for p in parts[:-1]:
            node = node.child(p)
            node.scope.add(sc)
        info = node.key(parts[-1])
        if method == "color":
            info["kind"] = "color"
        elif method == "choice":
            info["kind"] = "enum"
            info["choices"] = choices.get(arr, [])
        elif method == "custom":
            info["kind"] = CUSTOM_KIND.get(dotted)
        else:
            info["kind"] = "set"
            info["target"] = ("config", member)
    return root


# ---------------------------------------------------------------------------
# Descriptions and defaults from the @annotations
# ---------------------------------------------------------------------------

def load_annotations():
    sections = Parser().parse(str(CONFIG_HPP))
    out = {}
    for s in sections:
        name = s["name"].lower().strip()
        out[name] = s
    return out


# TOML key -> C++ member name, where the loader reads a key under another name
KEY_ALIAS = {
    ("behavior", "page_history"): "page_history_limit",
    ("jump_marker", "jump_marker"): "color",
}


def annotation_for(ann, path, key=None):
    """Annotation entry for a section path (tuple) and optionally a key."""
    dotted = ".".join(path)
    key = KEY_ALIAS.get((dotted, key), key)
    candidates = [dotted]
    # statusbar.components.X is `statusbar.component.X` in the C++ names
    candidates.append(dotted.replace("statusbar.components", "statusbar.component"))
    # annotations.<kind> share the `annotations.[type]` template
    if len(path) == 2 and path[0] == "annotations":
        candidates += ["annotations.[type]", f"annotations.{path[1]}"]
    if len(path) == 3 and path[0] == "annotations":
        candidates += [f"annotations.[type].{path[2]}", f"annotations.{path[1]}.{path[2]}"]
    # pickers share one set of keys
    if key and path and path[-1] != "picker" and key in (
            "width", "height", "border", "alternating_row_color", "style", "scrolloff", "max_lines"):
        candidates.append("picker")
    if len(path) >= 2 and path[-1] == "highlight_matches":
        candidates.append("picker.highlight_matches")
    if len(path) >= 2 and path[-1] == "shadow":
        candidates.append("picker.shadow")
    # the session component's option is filed under statusbar.component
    if dotted == "statusbar.components.session":
        candidates.append("statusbar.component")
    for c in candidates:
        s = ann.get(c)
        if not s:
            continue
        if key is None:
            return s
        for f in s["fields"]:
            if f["name"] == key:
                return f
    return None


def parse_default(text):
    if text is None:
        return None
    t = text.strip()
    for a, b in (("true", True), ("false", False)):
        if t == a:
            return b
    m = re.fullmatch(r"(-?\d+(?:\.\d+)?)[fF]?", t)
    if m:
        v = m.group(1)
        return float(v) if "." in v else int(v)
    m = re.fullmatch(r'"(.*)"', t)
    if m:
        return m.group(1)
    return None


def clean(text):
    if not text:
        return ""
    text = re.sub(r"<[^>]+>", "", text)          # drop the html used by the docs site
    return re.sub(r"\s+", " ", text).strip()


# ---------------------------------------------------------------------------
# Shapes the loader reads by hand (not derivable from `x["key"]` accesses)
# ---------------------------------------------------------------------------

STRING_OR_LIST = {"oneOf": [{"type": "string"}, {"type": "array", "items": {"type": "string"}}]}

SPECIAL = {
    ("preview", "size_ratio"): {
        "type": "object",
        "properties": {
            "width": {"type": "number", "description": "Width as a fraction of the main window (default 0.6)."},
            "height": {"type": "number", "description": "Height as a fraction of the main window (default 0.7)."},
        },
        "additionalProperties": False,
        "description": "Size of the preview window as a ratio of the main window.",
    },
    ("window", "initial_size"): {
        "type": "object",
        "properties": {
            "width": {"type": "integer", "description": "Initial window width in pixels."},
            "height": {"type": "integer", "description": "Initial window height in pixels."},
        },
        "additionalProperties": False,
        "description": "Initial window size in pixels.",
    },
    ("statusbar", "padding"): {
        "oneOf": [
            {"type": "integer"},
            {"type": "array", "items": {"type": "integer"}, "minItems": 4, "maxItems": 4},
        ],
        "description": "Statusbar padding in pixels: one number for all four sides, or [left, top, right, bottom].",
    },
    ("rendering", "dpr"): {
        "oneOf": [
            {"type": "number"},
            {"type": "object", "additionalProperties": {"type": "number"},
             "description": "Device pixel ratio per screen name."},
        ],
        "description": "Device pixel ratio: one number for every screen, or a table keyed by screen name.",
    },
    ("misc", "color_dialog_colors"): {
        "type": "array",
        "items": {"type": "string", "pattern": COLOR_PATTERN},
        "description": "Colours offered in the colour dialog.",
    },
    ("presentation", "allowed_actions"): {
        "type": "array",
        "items": {"type": "string"},
        "default": [],
        "description": "Extra actions that stay usable in presentation mode, besides navigation (since 0.8.0).",
    },
    ("llm_view", "extra_body"): {
        "type": "object",
        "description": "Extra fields merged into every request body sent to the LLM endpoint.",
    },
}

def view_sections(registry):
    """Sections that can be overridden per file type (scope View / PerFiletype)."""
    def per_filetype(scope):
        return "PerFiletype" in scope or re.search(r"\bView\b", scope)
    return [name for name, node in registry.children.items()
            if any(per_filetype(sc) for sc in node.scope)]


def picker_key_names(registry):
    keys = registry.children.get("picker", Node()).children.get("keys")
    return list(keys.keys) if keys else []


# ---------------------------------------------------------------------------
# Schema assembly
# ---------------------------------------------------------------------------

class Builder:
    def __init__(self):
        self.root_structs, self.structs = parse_structs(CONFIG_HPP)
        self.enums = parse_enums(CONFIG_HPP)
        self.loader = extract_registry(OPTIONS_CPP)
        self.ann = load_annotations()
        self.problems = []

    # -- a single key -------------------------------------------------------
    def key_schema(self, path, key, info):
        full = path + (key,)
        if full in SPECIAL:
            schema = dict(SPECIAL[full])
        else:
            schema = self.typed(info, path, key)

        ann = annotation_for(self.ann, path, key)
        if ann:
            desc = clean(ann.get("desc"))
            added = ann.get("added")
            if desc:
                schema.setdefault("description", desc + (f" (since {added})" if added else ""))
            if "default" not in schema:
                d = parse_default(ann.get("default"))
                if d is not None:
                    schema["default"] = d
        if schema.get("type") == "string" and ("enum" in schema or (ann and ann.get("choice"))):
            vals = list(schema.get("enum", []))
            if ann and ann.get("choice"):
                vals += re.findall(r'"([^"]+)"', ann["choice"]) or \
                    [c.strip() for c in ann["choice"].split(",") if c.strip()]
            # the option list names the choices; fall back to the C++ enum
            member = resolve_member(self.root_structs, ".".join(full))
            if member and not info.get("choices"):
                vals += self.enums.get(member[0].split("::")[-1], [])
            d = parse_default(ann.get("default")) if ann else None
            if isinstance(d, str):
                vals.append(d)
            schema["enum"] = list(dict.fromkeys(vals))
        return schema

    def typed(self, info, path, key):
        kind = info["kind"]
        schema = {}
        if kind == "color":
            schema = {"type": "string", "pattern": COLOR_PATTERN,
                      "description": 'Colour as "#RRGGBB" or "#RRGGBBAA".'}
        elif kind == "enum":
            schema = {"type": "string", "enum": info["choices"]}
        elif kind == "array":
            schema = {"type": "array"}
        elif kind == "table":
            schema = {"type": "object"}
        elif kind and kind.startswith("value:"):
            t = kind.split(":", 1)[1]
            schema = {"type": {"std::string": "string", "int": "integer", "bool": "boolean",
                               "float": "number", "double": "number", "int64_t": "integer"}.get(t, "string")}
        elif kind == "string":
            schema = {"type": "string"}
        elif info["target"]:
            origin, dotted = info["target"]
            member = resolve_member(self.root_structs,
                                    ("picker." if origin == "target" and path and False else "") + dotted)
            if member is None and origin == "target":
                member = resolve_member(self.root_structs, "picker." + dotted)
            if member:
                ctype, default = member
                schema = {"type": json_type_for_cpp(ctype)}
                d = cpp_default_to_json(default)
                if d is not None and schema["type"] in ("boolean", "integer", "number", "string"):
                    schema["default"] = d
            else:
                self.problems.append(f"no C++ type for [{'.'.join(path)}] {key} (target {dotted})")
                schema = {}
        else:
            self.problems.append(f"unknown kind for [{'.'.join(path)}] {key}")
        return schema

    # -- a table ------------------------------------------------------------
    def table_schema(self, path, node):
        props = collections.OrderedDict()
        for key, info in node.keys.items():
            if (path + (key,)) in SPECIAL or key not in node.children:
                props[key] = self.key_schema(path, key, info)
        for name, child in node.children.items():
            if (path + (name,)) in SPECIAL:
                props[name] = self.key_schema(path, name, {"kind": None, "target": None, "choices": []})
                continue
            props[name] = self.table_schema(path + (name,), child)
        # SPECIAL keys that the loader reads in a way the extractor skips
        for (p, k) in SPECIAL:
            if p == path and k not in props:
                props[k] = self.key_schema(path, k, {"kind": None, "target": None, "choices": []})

        schema = {"type": "object", "properties": props, "additionalProperties": False}
        ann = annotation_for(self.ann, path)
        if ann and ann.get("section_desc"):
            added = ann.get("section_added")
            schema["description"] = clean(ann["section_desc"]) + (f" (since {added})" if added else "")
        return schema

    def build(self):
        names = picker_key_names(self.loader)
        sections = collections.OrderedDict()
        for name, node in self.loader.children.items():
            if name == "picker":
                node.children.pop("keys", None)    # added below
            sections[name] = self.table_schema((name,), node)

        # [picker.keys]: read with get("name"), values are a key or a list of keys
        if "picker" in sections and names:
            keys = {n: dict(STRING_OR_LIST, description=f"Key(s) for {n.replace('_', ' ')}.") for n in names}
            sections["picker"]["properties"]["keys"] = {
                "type": "object", "properties": keys, "additionalProperties": False,
                "description": "Key bindings inside pickers (a key, or a list of keys).",
            }

        # [keybindings]: command name -> key(s); load_defaults is a switch
        sections["keybindings"] = {
            "type": "object",
            "description": "Key bindings: command name = key (or list of keys). Command names are listed by `lektra --list-commands`.",
            "properties": {"load_defaults": {
                "type": "boolean", "default": True,
                "description": "Load the built-in key bindings first. Set to false to start from an empty set."}},
            "additionalProperties": STRING_OR_LIST,
        }
        sections["mousebindings"] = {
            "type": "object",
            "description": 'Mouse bindings: action = "Modifier+Button", e.g. pan = "Alt+LeftButton".',
            "additionalProperties": {"type": "string"},
        }

        # [filetype.<type>.<section>]: per file type overrides of the view settings
        view = [s for s in view_sections(self.loader) if s in sections]
        sections["filetype"] = {
            "type": "object",
            "description": "Per-file-type overrides of the view settings, e.g. [filetype.epub.layout] or "
                           "dotted keys under [filetype.pdf]. The type is lower-case (pdf, epub, djvu, ...).",
            "additionalProperties": {
                "type": "object",
                "properties": {s: sections[s] for s in view},
                "additionalProperties": False,
            },
        }

        return {
            "$schema": "http://json-schema.org/draft-07/schema#",
            "$id": "https://codeberg.org/lektra/lektra/raw/branch/main/schema/lektra-config.schema.json",
            "title": "Lektra configuration",
            "description": "Schema for Lektra's config.toml, generated by scripts/gen_config_schema.py.",
            "type": "object",
            "properties": sections,
            "additionalProperties": False,
        }


# ---------------------------------------------------------------------------

def check(schema, problems):
    ok = True
    try:
        import jsonschema
        import tomllib
    except ImportError:
        print("check: needs the `jsonschema` package; skipped", file=sys.stderr)
        return True
    jsonschema.Draft7Validator.check_schema(schema)
    validator = jsonschema.Draft7Validator(schema)
    data = tomllib.loads(DEFAULT_TOML.read_text())
    errors = sorted(validator.iter_errors(data), key=lambda e: list(e.absolute_path))
    for e in errors:
        ok = False
        print(f"default_config.toml: {'.'.join(map(str, e.absolute_path))}: {e.message}", file=sys.stderr)
    for p in problems:
        print(f"note: {p}", file=sys.stderr)
    if ok:
        print("default_config.toml validates against the generated schema", file=sys.stderr)
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=OUT_DEFAULT)
    ap.add_argument("--stdout", action="store_true")
    ap.add_argument("--check", action="store_true", help="validate default_config.toml against the result")
    args = ap.parse_args()

    b = Builder()
    schema = b.build()
    text = json.dumps(schema, indent=2, ensure_ascii=False) + "\n"
    if args.stdout:
        sys.stdout.write(text)
    else:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text)
        print(f"wrote {args.out}", file=sys.stderr)
    for p in b.problems:
        print(f"note: {p}", file=sys.stderr)
    if args.check and not check(schema, []):
        sys.exit(1)


if __name__ == "__main__":
    main()
