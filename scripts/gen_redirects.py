#!/usr/bin/env python3
"""
Write redirect pages for the old website (GitHub Pages) that point to the new
documentation (Read the Docs).

The old URLs are in the binary (default_config.toml, Config.hpp), in the man
page, in the README and in the tutorial PDF of releases that cannot be changed,
so they must keep working. Replace the old pages with these, in the website
repository, once the new documentation is online:

    python scripts/gen_redirects.py --base https://lektra.readthedocs.io/en/latest/ \\
        --out ~/Gits/dheerajshenoy.github.io/lektra

The home page (index.html) is left alone: it stays the landing page with the
screenshots and videos. The old pages are in git history of the website repo.

A redirect page uses a <meta> refresh (works without JavaScript), says where the
page went and names the new address as canonical for search engines. A "#anchor"
of the old link is passed on.
"""

import argparse
import html
import sys
from pathlib import Path

# old page of the website -> page of the new documentation
PAGES = {
    "installation.html": "installation/",
    "commands.html": "reference/commands/",
    "configuration.html": "reference/configuration/",
    "lua_api.html": "reference/lua_api/",
    "about.html": "about/",
}

TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <title>Lektra documentation has moved</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta http-equiv="refresh" content="0; url={url}">
    <link rel="canonical" href="{url}">
    <script>location.replace({js_url} + location.hash);</script>
</head>
<body>
    <p>The Lektra documentation has moved to
    <a href="{url}">{url}</a>.</p>
</body>
</html>
"""


def js_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    ap.add_argument("--base", required=True,
                    help="address of the new documentation, e.g. "
                         "https://lektra.readthedocs.io/en/latest/")
    ap.add_argument("--out", required=True,
                    help="folder of the old pages, e.g. the lektra/ folder of the "
                         "website repository")
    ap.add_argument("--dry-run", action="store_true",
                    help="only say what would be written")
    args = ap.parse_args()

    if not args.base.startswith(("http://", "https://")):
        ap.error("--base must be a full address (https://...)")
    base = args.base if args.base.endswith("/") else args.base + "/"

    out = Path(args.out).expanduser()
    if not out.is_dir():
        ap.error(f"{out} is not a folder")

    for old, new in PAGES.items():
        url = base + new
        text = TEMPLATE.format(url=html.escape(url, quote=True), js_url=js_string(url))
        target = out / old
        action = "would write" if args.dry_run else "wrote"
        if not args.dry_run:
            target.write_text(text)
        print(f"{action} {target}  ->  {url}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
