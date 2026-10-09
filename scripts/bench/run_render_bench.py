#!/usr/bin/env python3
"""Measure Lektra's render pipeline and compare runs.

Runs scripts/bench/render_bench.lua inside Lektra with LEKTRA_RENDER_TRACE=1,
cuts the trace into scenarios and prints one table per document. Results can be
saved and compared against an earlier run, so each change to the pipeline gets
a before/after on paper.

    scripts/bench/run_render_bench.py FILE.pdf [FILE2.pdf ...] [options]

    --binary build/lektra       the Lektra to run (Release build)
    --runs 5                    repetitions per document (median is reported)
    --scenarios A,B,C,D         which scenarios (see render_bench.lua)
    --offscreen                 QT_QPA_PLATFORM=offscreen (no window; leaves
                                out the paint cost)
    --save before.json          write the numbers
    --compare before.json       also show the change against a saved run
    --timeout 180               seconds before a run is abandoned
    --env KEY=VALUE             extra environment variable for Lektra (repeatable),
                                e.g. --env SOME_SWITCH=1 for an A/B run

Metrics (per scenario, median over the runs):
    requests     renders asked for
    applied      renders that reached the screen (visible + preload)
    wasted       renders that finished but were dropped (cancelled,
                 superseded, or made at an old zoom)
    wasted_ms    worker time spent on those
    render_ms    median / p95 time to draw one page on a worker
    gui_ms       total GUI-thread time spent handling results
    gui_max_ms   the slowest single result on the GUI thread
    settle_ms    from the first request of a burst to the last result
    stop_ms      from the last input to the last page drawn: how long the page
                 you stopped on takes to appear
    rss_mb       peak resident memory of the whole run (VmHWM)
"""

import argparse
import json
import os
import re
import signal
import statistics
import subprocess
import sys
import threading
import time
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LUA = ROOT / "scripts" / "bench" / "render_bench.lua"
WASTED = {"cancelled", "superseded", "stale_zoom"}
APPLIED = {"applied", "applied_preload"}

LINE = re.compile(r"^RTRACE (\S+) (\S+) (.*)$")


def fields(text):
    return dict(p.split("=", 1) for p in text.split() if "=" in p)


def percentile(values, p):
    if not values:
        return 0.0
    values = sorted(values)
    k = min(len(values) - 1, int(round(p / 100.0 * (len(values) - 1))))
    return values[k]


def peak_rss_mb(pid):
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmHWM:"):
                return int(line.split()[1]) / 1024.0
    except OSError:
        pass
    return 0.0


def run_once(binary, document, scenarios, offscreen, timeout, extra_env=()):
    env = dict(os.environ)
    for item in extra_env:
        key, _, value = item.partition("=")
        env[key] = value
    env["LEKTRA_RENDER_TRACE"] = "1"
    env["LEKTRA_BENCH_SCENARIOS"] = scenarios
    if offscreen:
        env["QT_QPA_PLATFORM"] = "offscreen"

    cmd = [str(binary), "--foreground", "-c", str(LUA), str(document)]
    proc = subprocess.Popen(cmd, env=env, stderr=subprocess.PIPE,
                            stdout=subprocess.DEVNULL, text=True, bufsize=1)

    sections = defaultdict(list)  # scenario -> parsed trace lines
    state = {"current": None, "done": False, "rss": 0.0}
    input_end = {}  # scenario -> wall time its input stopped

    def reader():
        for raw in (proc.stderr or []):
            raw = raw.rstrip("\n")
            if raw.startswith("BENCH "):
                word = raw.split()[1:]
                if word and word[0] in ("start", "error"):
                    print("  " + raw, flush=True)
                    continue
                if word[0] == "begin":
                    state["current"] = word[1]
                elif word[0] == "inputend":
                    if state["current"]:
                        input_end[state["current"]] = time.time()
                elif word[0] == "end":
                    state["current"] = None
                elif word[0] == "done":
                    state["rss"] = peak_rss_mb(proc.pid)
                    state["done"] = True
                continue
            m = LINE.match(raw)
            if m and state["current"]:
                sections[state["current"]].append(
                    (float(m.group(1)), m.group(2), fields(m.group(3)),
                     time.time()))

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    deadline = time.time() + timeout
    while time.time() < deadline and not state["done"] and proc.poll() is None:
        time.sleep(0.2)

    ok = state["done"]
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill()
    t.join(2)
    return ok, sections, state["rss"], input_end


def summarize(lines, input_end=None):
    gui = [f for _, ev, f, _ in lines if ev == "gui"]
    reqs = [1 for _, ev, _, _ in lines if ev == "request"]
    settles = [float(f["ms"]) for _, ev, f, _ in lines if ev == "settle"]

    # From the last input until the renders then outstanding are done: how
    # long the page you stopped on takes to be drawn. 0 if everything asked
    # for was already drawn when the input stopped. (Wall clock; the runner
    # timestamps the lines as it reads them.)
    after_stop = 0.0
    if input_end:
        busy = False
        for _, ev, _, wall in sorted(lines, key=lambda l: l[3]):
            if wall >= input_end:
                if not busy:
                    break  # all drawn already
                if ev == "settle":
                    after_stop = (wall - input_end) * 1000.0
                    break
                continue
            if ev == "request":
                busy = True
            elif ev == "settle":
                busy = False

    applied = [g for g in gui if g["outcome"] in APPLIED]
    wasted = [g for g in gui if g["outcome"] in WASTED]
    render = [float(g["render_ms"]) for g in gui if float(g["render_ms"]) > 0]
    cb = [float(g["cb_ms"]) for g in gui]

    return {
        "requests": len(reqs),
        "applied": len(applied),
        "wasted": len(wasted),
        "wasted_ms": sum(float(g["render_ms"]) for g in wasted),
        "render_ms": statistics.median(render) if render else 0.0,
        "render_p95_ms": percentile(render, 95),
        "gui_ms": sum(cb),
        "gui_max_ms": max(cb) if cb else 0.0,
        "settle_ms": max(settles) if settles else 0.0,
        "after_stop_ms": after_stop,
    }


def median_of(runs):
    keys = runs[0].keys()
    return {k: statistics.median(r[k] for r in runs) for k in keys}


COLUMNS = [
    ("requests", "requests", "{:.0f}"),
    ("applied", "applied", "{:.0f}"),
    ("wasted", "wasted", "{:.0f}"),
    ("wasted_ms", "wasted_ms", "{:.0f}"),
    ("render_ms", "render_ms", "{:.1f}"),
    ("render_p95_ms", "p95_ms", "{:.1f}"),
    ("gui_ms", "gui_ms", "{:.0f}"),
    ("gui_max_ms", "gui_max", "{:.1f}"),
    ("settle_ms", "settle_ms", "{:.0f}"),
    ("after_stop_ms", "stop_ms", "{:.0f}"),
    ("rss_mb", "rss_mb", "{:.0f}"),
]


def delta(new, old):
    if old == 0:
        return "n/a" if new == 0 else "+inf"
    return f"{(new - old) / old * 100:+.0f}%"


def print_table(title, results, baseline):
    print(f"\n=== {title} ===")
    header = ["scenario"] + [c[1] for c in COLUMNS]
    rows = []
    for scen in sorted(results):
        r = results[scen]
        rows.append([scen] + [c[2].format(r.get(c[0], 0.0)) for c in COLUMNS])
        if baseline and scen in baseline:
            b = baseline[scen]
            rows.append(["  vs saved"] + [delta(r.get(c[0], 0.0),
                                                b.get(c[0], 0.0))
                                          for c in COLUMNS])
    widths = [max(len(str(x)) for x in col) for col in zip(header, *rows)]
    fmt = "  ".join("{:>%d}" % w for w in widths)
    print(fmt.format(*header))
    for row in rows:
        print(fmt.format(*row))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("documents", nargs="+")
    ap.add_argument("--binary", default=str(ROOT / "build" / "lektra"))
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--scenarios", default="A,B,C,D")
    ap.add_argument("--offscreen", action="store_true")
    ap.add_argument("--save")
    ap.add_argument("--compare")
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--env", action="append", default=[])
    args = ap.parse_args()

    if not Path(args.binary).exists():
        sys.exit(f"binary not found: {args.binary}")

    # Numbers from a Debug or sanitizer build say little about the real thing:
    # AddressSanitizer alone intercepts every allocation and memset.
    try:
        linked = subprocess.run(["ldd", args.binary], capture_output=True,
                                text=True).stdout
    except OSError:
        linked = ""
    if "libasan" in linked or "libubsan" in linked:
        print("WARNING: this binary is built with a sanitizer (AddressSanitizer).\n"
              "         Build with -DCMAKE_BUILD_TYPE=Release (or -DWITH_ASAN=OFF)\n"
              "         for numbers that mean anything.\n", flush=True)

    saved = json.loads(Path(args.compare).read_text()) if args.compare else {}
    everything = {}

    for doc in args.documents:
        per_scenario = defaultdict(list)
        rss_runs = []
        for i in range(args.runs):
            print(f"{doc}: run {i + 1}/{args.runs} ...", flush=True)
            ok, sections, rss, input_end = run_once(args.binary, doc, args.scenarios,
                                         args.offscreen, args.timeout,
                                         args.env)
            if not ok:
                print("  (did not finish; run discarded)")
                continue
            rss_runs.append(rss)
            for scen, lines in sections.items():
                per_scenario[scen].append(
                    summarize(lines, input_end.get(scen)))

        if not per_scenario:
            print(f"{doc}: no usable runs "
                  "(is LEKTRA_RENDER_TRACE in this build, and is there a "
                  "display? try --offscreen)")
            continue

        result = {}
        for scen, runs in per_scenario.items():
            result[scen] = median_of(runs)
            result[scen]["rss_mb"] = statistics.median(rss_runs)
            result[scen]["runs"] = len(runs)
        everything[os.path.basename(doc)] = result
        print_table(f"{os.path.basename(doc)}  (median of "
                    f"{len(rss_runs)} runs)", result,
                    saved.get(os.path.basename(doc)))

    if args.save:
        Path(args.save).write_text(json.dumps(everything, indent=2))
        print(f"\nsaved {args.save}")


if __name__ == "__main__":
    main()
