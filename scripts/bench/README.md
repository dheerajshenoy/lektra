# Render benchmark

Measures the render pipeline so a change can be judged with numbers.

## Run

```
cmake --build build                       # Release build, same flags every time
python3 scripts/bench/run_render_bench.py \
    ~/docs/text.pdf ~/docs/scans.pdf ~/docs/links.pdf \
    --runs 5 --save before.json
```

Make a change, rebuild, then:

```
python3 scripts/bench/run_render_bench.py ~/docs/text.pdf ... --runs 5 \
    --compare before.json --save after.json
```

Each scenario gets a "vs saved" row with the change in percent. Use the same
documents, the same machine and nothing else heavy running. A real window
needs a display; `--offscreen` works without one but leaves out the paint
cost, so it cannot show the effect of a pixel-format change.

## Show saved results

```
python3 scripts/bench/show_render_bench.py before.json after1.json after2.json
```

One table per document and scenario, a column per file, and from the second
file on a change column against the first. Lower is better for every metric, so
a negative change is an improvement. `--only A,B`, `--doc NAME` and
`--metrics wasted_ms,settle_ms` narrow the output.

## Scenarios

| | does | exercises |
|---|---|---|
| A | 200 scroll steps of about a screen, 16 ms apart | queueing, cancellation, item churn |
| B | 30 zoom steps of +10 %, 60 ms apart | cancellation of stale renders |
| C | zoom to 8x, then 100 pan steps | partial-region renders and refreshes |

## Columns

| | meaning |
|---|---|
| requests | renders asked for |
| applied | renders that reached the screen (visible and preload) |
| wasted | renders that finished but were dropped (cancelled, superseded, old zoom) |
| wasted_ms | worker time spent on those |
| render_ms / p95_ms | median / 95th percentile time to draw one page on a worker |
| gui_ms | total GUI-thread time spent handling finished renders |
| gui_max | slowest single result on the GUI thread |
| settle_ms | first request of a burst to the last result |
| rss_mb | peak resident memory of the run |

## What each planned change should move

| change | look at |
|---|---|
| cancel running renders (`fz_cookie`) | wasted_ms down, settle_ms down in B and C |
| no full-page copy | rss_mb, render_ms |
| reuse the page item | gui_ms, gui_max |
| skip rebuilding links/annotations | gui_ms in C and in link-heavy documents |
| pixel format | paint cost: needs a real window, check with `perf` |

## Raw trace

`LEKTRA_RENDER_TRACE=1 lektra ...` prints lines on stderr:

```
RTRACE <ms> request page=.. visible=.. force=..
RTRACE <ms> worker  page=.. queue_ms=.. cache_ms=.. render_ms=.. w=.. h=.. partial=.. zoom=..
RTRACE <ms> gui     page=.. outcome=.. cb_ms=.. render_ms=.. queue_ms=.. cache_ms=..
RTRACE <ms> settle  ms=.. requests=..
```

`outcome` is one of `applied`, `applied_preload`, `cancelled`, `superseded`,
`stale_zoom`, `null_image`. With the variable unset the calls cost one branch.
