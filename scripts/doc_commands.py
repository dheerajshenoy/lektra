#!/usr/bin/env python3
"""
Extract command name + description pairs from Lektra::initCommands().
Usage: python doc_commands.py [source file or folder]
"""

import re
import sys
import json
import os

HOME = os.getenv("HOME")


_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def extract_commands(path: str) -> list[tuple[str, str]]:
    with open(path, "r") as f:
        source = f.read()

    # m_command_manager->reg("name", tr("description"), ...). The description
    # may be written as several adjacent string literals, which C++ joins.
    pattern = re.compile(
        r'm_command_manager->reg\(\s*"([^"]+)"\s*,\s*(?:tr\()?\s*'
        r'((?:"(?:[^"\\]|\\.)*"\s*)+)',
        re.MULTILINE,
    )

    commands = []
    for name, literals in pattern.findall(source):
        desc = "".join(_LITERAL.findall(literals)).replace('\\"', '"')
        commands.append((name, desc))
    return commands


def extract_all(path: str) -> list[tuple[str, str]]:
    """The commands of a source file, or of every .cpp file below a folder
    (the commands are registered in more than one file)."""
    if os.path.isfile(path):
        return extract_commands(path)

    seen = set()
    commands = []
    for folder, _, files in sorted(os.walk(path)):
        for name in sorted(files):
            if not name.endswith(".cpp"):
                continue
            for command, desc in extract_commands(os.path.join(folder, name)):
                if command not in seen:
                    seen.add(command)
                    commands.append((command, desc))
    return commands


def main():
    default = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")
    path = sys.argv[1] if len(sys.argv) > 1 else default

    commands = extract_all(path)

    if not commands:
        print("No commands found.")
        return

    out = [{"name": name, "description": desc} for name, desc in commands]
    with open(
        f"{HOME}/Gits/dheerajshenoy.github.io/lektra/files/commands.json", "w"
    ) as f:
        f.write(json.dumps(out, indent=2))

if __name__ == "__main__":
    main()
