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
# once, each into its own build/sweep/pad<n>/build (FELUCCA_OUT), then runs its scenarios; the emulator counts device
# cycles, so parallel runs give the same numbers. build/ itself is not touched; images and results in build/sweep/.
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

# one layout: its build (pad 0: none), then its scenarios
layout() {
    d="$OUT/pad$1"
    pad=$1
    [ "$pad" = 0 ] && pad=
    mkdir -p "$d"
    if ! FELUCCA_OUT="$d/build" FELUCCA_LAYOUT_PAD="$pad" ./build.sh >"$d/build.log" 2>&1; then
        echo "layout_sweep: pad $1: build failed ($d/build.log)" >&2
        return 1
    fi
    for s in $SCENARIOS; do
        "$RUNNER" --fwsc "$d/build/felucca.fwsc" --elf "$d/build/felucca.elf" --scenario "${s%%:*}" --ms "${s##*:}" \
            2>"$d/${s%%:*}.log" | grep '^{' >"$d/${s%%:*}.json" &
    done
    wait
}
for n in $LAYOUTS; do
    layout "$n" &
done
wait || true                                    # (a failed layout shows below as a missing result)

python3 - "$OUT" "$LAYOUTS" "$SCENARIOS" <<'EOF'
import json, os, statistics, sys
out, layouts, scenarios = sys.argv[1], sys.argv[2].split(), [s.split(":")[0] for s in sys.argv[3].split()]
bad = 0
for sc in scenarios:
    rows = []
    for n in layouts:
        p = os.path.join(out, "pad" + n, sc + ".json")
        if not os.path.exists(p):                 # its build failed: no image to run
            print(f"layout_sweep: {sc} pad {n}: no image ({os.path.join(out, 'pad' + n, 'build.log')})")
            bad = 1
            continue
        t = open(p).read().strip()
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
