import json
import sys
import os

class Parser:
    def __init__(self):
        self.HOME: str | None = os.getenv("HOME")
        self.file = None
        self.lines = None
        self.keywords = ["desc", "choice", "type", "default", "note", "added"]

    def __open(self, file: str):
        if file.startswith("~"):
            file = file.replace("~", self.HOME)
        self.file = file

        if not os.path.isfile(file):
            raise FileNotFoundError(f"Could not find file: {file}")

        try:
            with open(self.file, "r") as f:
                self.lines = [line.rstrip() for line in f.readlines()]
        except OSError:
            print(f"Could not open file: {self.file}")
            sys.exit(-1)

    def parse(self, path: str):
        self.__open(path)

        sections = []
        current_section = None
        current_tags = {}
        active_block_key = None

        section_keywords = {f"section_{kw}": kw for kw in self.keywords}

        for line in self.lines:
            line = line.strip()
            if not line:
                continue

            # TAG LINE
            if line.startswith("// @"):
                content = line[4:].strip()
                key, _, value = content.partition(" ")
                key, value = key.strip(), value.strip()

                # Section start
                if key == "section":
                    current_section = {
                            "name": value,
                            "fields": [],
                            }
                    # Initialize section-level tags
                    current_section.update({skw: "" for skw in section_keywords})
                    current_tags = {}
                    active_block_key = None
                    continue

                # Section-level block start
                if key.startswith("section_") and value.endswith("{"):
                    active_block_key = key
                    current_section[active_block_key] = ""
                    continue

                # Inline section-level metadata
                if key.startswith("section_"):
                    current_section[key] = value
                    continue

                # Section end
                if key == "endsection":
                    if current_section:
                        sections.append(current_section)
                    current_section = None
                    current_tags = {}
                    active_block_key = None
                    continue

                # Field-level tag
                if key in self.keywords:
                    if value.startswith("{"):
                        if value.endswith("}"):
                            current_tags[key] = value[1:-1].strip()
                        else:
                            current_tags[key] = value[1:].strip()
                            active_block_key = key
                    else:
                        current_tags[key] = value
                    continue

            elif line.startswith("//") and active_block_key:
                content = line[2:].strip()
                closing = content.endswith("}")
                chunk = content[:-1].strip() if closing else content

                # Determine which dict we're accumulating into
                target = current_section if (current_section and active_block_key in section_keywords) else current_tags

                # Use newline separator inside <pre>, space otherwise
                current_text = target[active_block_key]
                in_pre = "<pre" in current_text and "</pre>" not in current_text
                separator = "\n" if in_pre else " "

                target[active_block_key] = (current_text + separator + chunk).strip()

                if closing:
                    active_block_key = None
                continue

            if current_section and current_tags and not line.startswith("//"):
                code_part = line.split("{")[0].strip()
                if code_part:
                    parts = code_part.split()
                    current_tags["name"] = parts[1]
                    if "type" not in current_tags:
                        current_tags["type"] = " ".join(parts[:-1])
                    current_section["fields"].append(current_tags)
                    current_tags = {}
                    active_block_key = None

        return sections

def main():
    parser = Parser()
    docobjs = parser.parse("../src/Config.hpp")
    USER = os.getenv("USER")
    with open(f"/home/{USER}/Gits/dheerajshenoy.github.io/lektra/files/config.json", 'w') as f:
        json.dump(docobjs, f, indent=4)


if __name__ == "__main__":
    main()
