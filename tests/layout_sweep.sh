#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
#
# The device-timed emulator over several flash layouts of the same firmware. A third of each half goes to flash
# cache misses, and code moving by a few hundred bytes changes which hot lines share a cache set: one build's
# cpu_pct can sit ~5 % off its neighbours (docs/fm1-emu-test.md). This builds the working tree once per layout
# (FELUCCA_LAYOUT_PAD: 8 x n stores that never run, perform.c), runs each scenario on each image, and prints
# every layout and the median (min - max). Judge a change by the medians and ranges, not by one build.
#   tests/layout_sweep.sh [scenario:ms ...]      default: dry:3000 keys:9000 churn:6000 stack:6000
#   LAYOUTS="0 1 2 3 4 5 6 7"                    the pads (0: none), 8 by default
# Needs the emulator runner (FM1_EMU=1 tests/run_tests.sh once, or docs/fm1-emu-test.md). Every layout builds at
# once, each into its own build/sweep/pad<n>/build (FELUCCA_OUT), retained for incremental builds. Each layout boots
# the emulator once for all its scenarios; layouts run in parallel. Images and results stay in build/sweep/.
set -e

cd "$(dirname "$0")/.."

RUNNER=build/emu-target/release/emu_test
OUT=build/sweep
LAYOUTS=${LAYOUTS:-"0 1 2 3 4 5 6 7"}
SCENARIOS=${*:-"dry:3000 keys:9000 churn:6000 stack:6000"}

if [ ! -x "$RUNNER" ]; then
    echo "layout_sweep: no $RUNNER (FM1_EMU=1 tests/run_tests.sh builds it)" >&2
    exit 1
fi
mkdir -p "$OUT"

# one layout: its incremental build (pad 0: none), then all scenarios in one boot
layout() {
    d="$OUT/pad$1"
    pad=$1
    [ "$pad" = 0 ] && pad=
    mkdir -p "$d"
    rm -f "$d"/*.json "$d/results.jsonl" "$d/results.tmp" "$d/run.log"
    if ! FELUCCA_OUT="$d/build" FELUCCA_LAYOUT_PAD="$pad" ./build.sh >"$d/build.log" 2>&1; then
        echo "layout_sweep: pad $1: build failed ($d/build.log)" >&2
        return 1
    fi
    if ! "$RUNNER" --fwsc "$d/build/felucca.fwsc" --elf "$d/build/felucca.elf" --scenarios "$SCENARIOS" --clear \
        >"$d/results.tmp" 2>"$d/run.log"; then
        echo "layout_sweep: pad $1: emulator failed ($d/run.log)" >&2
        return 1
    fi
    mv "$d/results.tmp" "$d/results.jsonl"
}
for n in $LAYOUTS; do
    layout "$n" &
done
wait || true                                    # a failed layout publishes no results.jsonl

python3 - "$OUT" "$LAYOUTS" "$SCENARIOS" <<'EOF'
import json
from pathlib import Path
import statistics
import sys

out, layouts = Path(sys.argv[1]), sys.argv[2].split()
scenarios = [(name, int(ms)) for name, ms in (s.split(":") for s in sys.argv[3].split())]
rows = {name: [] for name, _ in scenarios}
bad = 0
for n in layouts:
    folder = out / ("pad" + n)
    path = folder / "results.jsonl"
    if not path.exists():
        print(f"layout_sweep: pad {n}: no successful result (see {folder / 'build.log'} and {folder / 'run.log'})")
        bad = 1
        continue
    try:
        results = [json.loads(line) for line in path.read_text().splitlines()]
        if [(r["scenario"], r["ms"]) for r in results] != scenarios:
            raise ValueError("results do not match the requested scenarios and durations")
        for r in results:
            rows[r["scenario"]].append((n, r["cpu_pct"], r["late"]))
    except (ValueError, KeyError, TypeError) as e:
        print(f"layout_sweep: pad {n}: invalid result: {e} ({path})")
        bad = 1
for sc, values in rows.items():
    if values:
        print(f"{sc}: " + "  ".join(f"pad {n} {c:.1f} % {l} late" for n, c, l in values))
        c = [r[1] for r in values]
        l = [r[2] for r in values]
        print(f"{sc}: cpu_pct median {statistics.median(c):.1f} ({min(c):.1f} - {max(c):.1f}), "
              f"late median {statistics.median(l):g} ({min(l)} - {max(l)}), {len(values)} layouts")
sys.exit(bad)
EOF
