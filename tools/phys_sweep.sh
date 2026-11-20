#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The PHYS sweep on the device-timed FM-1 emulator (tests/emu): three project stores, one JSON line each.
#   tools/phys_sweep.sh [--emu PATH] [--fwsc PATH] [--elf PATH]
# Takes build/felucca.fwsc and build/felucca.elf from ./build.sh; the runner is built by tests/run_tests.sh
# (FM1_EMU=1) or by hand:
#   cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -lm tests/emu/ids.c -o build/host/emu_ids
#   build/host/emu_ids > build/emu/ids.rs
#   cargo build --release --offline -q --manifest-path tests/emu/Cargo.toml --target-dir build/emu-target
# What each case is: tests/emu/phys/README.md. The load is printed, not bounded (docs/fm1-emu-test.md).
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
dir="$root/tests/emu/phys"
emu="$root/build/emu-target/release/emu_test"
fwsc="$root/build/felucca.fwsc"
elf="$root/build/felucca.elf"
while [ $# -gt 0 ]; do
    case $1 in
        --emu) emu=$2; shift 2 ;;
        --fwsc) fwsc=$2; shift 2 ;;
        --elf) elf=$2; shift 2 ;;
        *) echo "phys_sweep: unknown option $1" >&2; exit 2 ;;
    esac
done
for f in "$emu" "$fwsc" "$elf"; do
    [ -f "$f" ] || { echo "phys_sweep: no $f (build first)" >&2; exit 1; }
done

run() {
    name=$1 ms=$2
    shift 2
    printf '%s ' "$name"
    "$emu" --fwsc "$fwsc" --elf "$elf" --scenario project --ms "$ms" \
        --project "$dir/$name.bin" --notes 3 --clear --seed 1 "$@" 2>/dev/null | tail -1
}

run busy_phys 1500
run psy_phys 1500
run psy_stack_p1_lfo 3000 --punch 1:F3,1:A3,1:C4,1:D4,1:E4,1:F4,1:E5,1:F5 --punch-latch
