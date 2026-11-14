#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Fetch fm1-emu (Joakim Eriksson, MIT: github.com/joakimeriksson/fm1-emu) at the pinned commit into DEST,
# for the emulator test (tests/emu, FM1_EMU=1 in tests/run_tests.sh). Only the workspace manifest, LICENSE and
# crates/ are checked out (the crates inherit their version from the workspace root).
#   tools/get_fm1emu.sh [DEST]     (default: build/deps/fm1-emu)
# To update: set PIN to a newer commit, run it, run FM1_EMU=1 tests/run_tests.sh.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
dest="${1:-$root/build/deps/fm1-emu}"
url=https://github.com/joakimeriksson/fm1-emu.git
PIN=c7e47c4fcbdaafb4beffa40493da1b2b07ffa263   # release 0.5.1 (2026-11-14)

if [ -d "$dest/.git" ] && [ "$(git -C "$dest" rev-parse HEAD 2>/dev/null)" = "$PIN" ]; then
    echo "get_fm1emu: $dest is at $PIN"
    exit 0
fi
rm -rf "$dest"
mkdir -p "$dest"
git -C "$dest" init --quiet
git -C "$dest" remote add origin "$url"
git -C "$dest" config core.sparseCheckout true
printf '/Cargo.toml\n/Cargo.lock\n/LICENSE\n/crates/\n' > "$dest/.git/info/sparse-checkout"
n=0
until git -C "$dest" fetch --quiet --depth 1 origin "$PIN"; do
    n=$((n + 1))
    [ "$n" -le 3 ] || { echo "get_fm1emu: fetch failed after $n attempts" >&2; exit 1; }
    echo "get_fm1emu: fetch attempt $n/3" >&2
    sleep $((n * 10))
done
git -C "$dest" -c advice.detachedHead=false checkout --quiet FETCH_HEAD
[ "$(git -C "$dest" rev-parse HEAD)" = "$PIN" ] || { echo "get_fm1emu: $dest is not at $PIN" >&2; exit 1; }
echo "FM1_EMU_DIR=$dest"
