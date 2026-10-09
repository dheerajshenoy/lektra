#!/usr/bin/env python3
"""Print saved render benchmark results (--save of run_render_bench.py).

    scripts/bench/show_render_bench.py before.json [after.json ...]

One table per document and scenario, metrics down the side and one column per
file. From the second file on, each column is followed by its change against
the first file. For every metric here lower is better, so a negative change is
an improvement.

    --only A,B          only these scenarios
    --doc NAME          only documents whose name contains NAME
    --metrics a,b,...   only these metrics (names as in the JSON)
"""

import argparse
import json
import sys
from pathlib import Path

METRICS = [
    ("requests", "requests", "{:.0f}"),
    ("applied", "applied", "{:.0f}"),
    ("wasted", "wasted renders", "{:.0f}"),
    ("wasted_ms", "wasted worker ms", "{:.0f}"),
    ("render_ms", "render ms (median)", "{:.1f}"),
    ("render_p95_ms", "render ms (p95)", "{:.1f}"),
    ("gui_ms", "GUI ms (total)", "{:.0f}"),
    ("gui_max_ms", "GUI ms (slowest)", "{:.1f}"),
    ("settle_ms", "settle ms", "{:.0f}"),
    ("rss_mb", "peak RSS MB", "{:.0f}"),
]


def change(new, old):
    if old == 0:
        return "-" if new == 0 else "new"
    return f"{(new - old) / old * 100:+.0f}%"


def table(rows):
    widths = [max(len(str(r[i])) for r in rows) for i in range(len(rows[0]))]
    for n, row in enumerate(rows):
        cells = [str(row[0]).ljust(widths[0])]
        cells += [str(c).rjust(w) for c, w in zip(row[1:], widths[1:])]
        print("  " + "  ".join(cells))
        if n == 0:
            print("  " + "  ".join("-" * w for w in widths))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--only")
    ap.add_argument("--doc")
    ap.add_argument("--metrics")
    args = ap.parse_args()

    data = []
    for f in args.files:
        try:
            data.append((Path(f).stem, json.loads(Path(f).read_text())))
        except (OSError, ValueError) as e:
            sys.exit(f"{f}: {e}")

    only = set(args.only.upper().split(",")) if args.only else None
    wanted = set(args.metrics.split(",")) if args.metrics else None
    metrics = [m for m in METRICS if not wanted or m[0] in wanted]

    docs = []
    for _, d in data:
        for name in d:
            if name not in docs and (not args.doc or args.doc in name):
                docs.append(name)

    if not docs:
        sys.exit("nothing to show")

    for doc in docs:
        scenarios = sorted({s for _, d in data for s in d.get(doc, {})})
        for scen in scenarios:
            if only and scen not in only:
                continue

            header = ["metric"]
            for i, (name, _) in enumerate(data):
                header.append(name)
                if i > 0:
                    header.append("vs " + data[0][0])

            runs = [d.get(doc, {}).get(scen, {}).get("runs") for _, d in data]
            shown = ", ".join(str(r) if r else "-" for r in runs)
            print(f"\n=== {doc} / scenario {scen}  (runs: {shown}) ===")

            rows = [header]
            for key, label, fmt in metrics:
                row = [label]
                first = None
                for i, (_, d) in enumerate(data):
                    r = d.get(doc, {}).get(scen)
                    if r is None or key not in r:
                        row.append("-")
                        if i > 0:
                            row.append("")
                        continue
                    value = r[key]
                    row.append(fmt.format(value))
                    if i == 0:
                        first = value
                    else:
                        row.append(change(value, first)
                                   if first is not None else "")
                rows.append(row)
            table(rows)


if __name__ == "__main__":
    main()
