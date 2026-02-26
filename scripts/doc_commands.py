#!/usr/bin/env python3
"""
Extract command name + description pairs from Lektra::initCommands().
Usage: python extract_commands.py <source_file>
"""

import re
import sys
import json


def extract_commands(path: str) -> list[tuple[str, str]]:
    with open(path, "r") as f:
        source = f.read()

    # Match m_command_manager.reg("name", "description", ...)
    pattern = re.compile(
        r'm_command_manager\.reg\(\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,',
        re.MULTILINE,
    )

    return pattern.findall(source)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "Lektra.cpp"

    commands = extract_commands(path)

    if not commands:
        print("No commands found.")
        return

    out = [{"name": name, "description": desc} for name, desc in commands]
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
