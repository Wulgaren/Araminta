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
# Needs the emulator runner (FM1_EMU=1 tests/run_tests.sh once, or docs/fm1-emu-test.md). Builds run one at a
# time (they share build/); the emulator counts device cycles, so its runs go in parallel with the same numbers.
# Leaves build/ as a plain ./build.sh; images and results in build/sweep/.
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
rm -rf "$OUT"
mkdir -p "$OUT"
trap './build.sh >/dev/null 2>&1' EXIT        # build/ back to the plain firmware, however this ends

for n in $LAYOUTS; do
    if [ "$n" = 0 ]; then
        ./build.sh >"$OUT/build$n.log" 2>&1 || { echo "layout_sweep: pad $n: build failed ($OUT/build$n.log)" >&2; exit 1; }
    else
        FELUCCA_LAYOUT_PAD=$n ./build.sh >"$OUT/build$n.log" 2>&1 ||
            { echo "layout_sweep: pad $n: build failed ($OUT/build$n.log)" >&2; exit 1; }
    fi
    mkdir -p "$OUT/pad$n"
    cp build/felucca.fwsc build/felucca.elf "$OUT/pad$n/"
    for s in $SCENARIOS; do
        "$RUNNER" --fwsc "$OUT/pad$n/felucca.fwsc" --elf "$OUT/pad$n/felucca.elf" --scenario "${s%%:*}" --ms "${s##*:}" \
            2>"$OUT/pad$n/${s%%:*}.log" | grep '^{' >"$OUT/pad$n/${s%%:*}.json" &
    done
    echo "layout_sweep: pad $n built, its scenarios running"
done
wait

python3 - "$OUT" "$LAYOUTS" "$SCENARIOS" <<'EOF'
import json, os, statistics, sys
out, layouts, scenarios = sys.argv[1], sys.argv[2].split(), [s.split(":")[0] for s in sys.argv[3].split()]
bad = 0
for sc in scenarios:
    rows = []
    for n in layouts:
        t = open(os.path.join(out, "pad" + n, sc + ".json")).read().strip()
        if not t:                                 # the runner failed its own checks (a punch-in not engaged, ...)
            log = os.path.join(out, "pad" + n, sc + ".log")
            last = open(log).read().strip().splitlines()[-1:] if os.path.exists(log) else ["(no log)"]
            print(f"layout_sweep: {sc} pad {n}: no result: {last[0]} ({log})")
            bad = 1
            continue
        j = json.loads(t)
        rows.append((n, j["cpu_pct"], j["late"], j["halves"]))
    if rows:
        print(f"{sc}: " + "  ".join(f"pad {n} {c:.1f} % {l} late" for n, c, l, _ in rows))
        c = [r[1] for r in rows]
        l = [r[2] for r in rows]
        print(f"{sc}: cpu_pct median {statistics.median(c):.1f} ({min(c):.1f} - {max(c):.1f}), "
              f"late median {statistics.median(l):g} ({min(l)} - {max(l)}), {len(rows)} layouts")
sys.exit(bad)
EOF
