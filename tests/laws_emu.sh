#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
#
# Generate covering project bins and run emu_test --batch in parallel.
#   tests/laws_emu.sh [JOBS]     default JOBS = min(4, nproc)
set -e

cd "$(dirname "$0")/.."

nproc=$(sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
if [ -n "${1:-}" ]; then
    JOBS=$1
else
    JOBS=$nproc
    if [ "$JOBS" -gt 4 ]; then JOBS=4; fi
fi
if [ "$JOBS" -lt 1 ]; then JOBS=1; fi

FWSC=build/felucca.fwsc
ELF=build/felucca.elf
EMU=build/emu-target/release/emu_test
OUT=build/laws_emu
GEN=build/host/laws_emu_gen

if [ ! -f "$FWSC" ]; then
    echo "run ./build.sh first" >&2
    exit 2
fi
if [ ! -f "$ELF" ]; then
    echo "run ./build.sh first" >&2
    exit 2
fi
if [ ! -x "$EMU" ]; then
    echo "== laws-emu: building emu_test"
    cargo build --release --offline -q --manifest-path tests/emu/Cargo.toml --target-dir build/emu-target
fi

mkdir -p build/host "$OUT"
echo "== laws-emu: cc -o $GEN tests/laws_emu_gen.c"
cc -O1 -w -Ibuild/gen -Ifirmware/src tests/laws_emu_gen.c -o "$GEN" -lm
"$GEN" "$OUT"

# Split manifest: heavy.bin lines in chunk 0; other path-groups round-robin.
python3 - "$OUT" "$JOBS" <<'PY'
import sys
from pathlib import Path
from collections import OrderedDict

out = Path(sys.argv[1])
jobs = int(sys.argv[2])
lines = [ln.rstrip("\n") for ln in (out / "manifest.txt").read_text().splitlines()
         if ln.strip() and not ln.lstrip().startswith("#")]
groups = OrderedDict()
for ln in lines:
    parts = ln.split()
    path = parts[2] if len(parts) >= 3 else ""
    groups.setdefault(path, []).append(ln)

chunks = [[] for _ in range(jobs)]
rr = 0
for path, glines in groups.items():
    if path.endswith("/heavy.bin") or path.endswith("heavy.bin"):
        chunks[0].extend(glines)
    else:
        chunks[rr % jobs].extend(glines)
        rr += 1

for i, ch in enumerate(chunks):
    p = out / f"batch.{i}.txt"
    p.write_text("\n".join(ch) + ("\n" if ch else ""))
PY

fail=0
pids=
i=0
while [ "$i" -lt "$JOBS" ]; do
    batch="$OUT/batch.$i.txt"
    if [ ! -s "$batch" ]; then
        : >"$OUT/out.$i.jsonl"
        i=$((i + 1))
        continue
    fi
    echo "== laws-emu: emu_test --batch $batch"
    "$EMU" --fwsc "$FWSC" --elf "$ELF" --batch "$batch" --notes 3 \
        >"$OUT/out.$i.jsonl" 2>"$OUT/err.$i.log" &
    pids="$pids $!"
    i=$((i + 1))
done

for pid in $pids; do
    if ! wait "$pid"; then
        fail=1
    fi
done

python3 - "$OUT" <<'PY'
import json, sys, fnmatch
from pathlib import Path

out = Path(sys.argv[1])
budgets = []
for ln in (out / "budgets.txt").read_text().splitlines():
    ln = ln.strip()
    if not ln or ln.startswith("#"):
        continue
    glob, lim = ln.split()
    budgets.append((glob, int(lim)))

def late_max(label):
    for glob, lim in budgets:
        if fnmatch.fnmatch(label, glob):
            return lim
    return 5

n = ok = over = 0
status = 0
for path in sorted(out.glob("out.*.jsonl")):
    text = path.read_text().strip()
    if not text:
        continue
    for ln in text.splitlines():
        try:
            row = json.loads(ln)
        except json.JSONDecodeError:
            print(f"BAD json: {ln[:80]}")
            status = 1
            continue
        label = row.get("case") or row.get("scenario") or "?"
        late = int(row.get("late", 0))
        lim = late_max(label)
        n += 1
        if late > lim:
            over += 1
            status = 1
            flag = "OVER"
        else:
            ok += 1
            flag = "ok"
        print(f"{label:24} late={late:4} budget={lim:3} {flag}")

print(f"laws-emu: {n} cases, {ok} late-ok, {over} over budget")
sys.exit(status)
PY
status=$?
if [ "$fail" -ne 0 ]; then
    echo "laws-emu: emu_test failed (see $OUT/err.*.log)" >&2
    exit 1
fi
exit "$status"
