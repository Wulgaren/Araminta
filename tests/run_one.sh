#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
#
# Fast, targeted host test runner for the edit loop. Run after ./build.sh once.
# Builds without -O1 unless a target needs it: the single-TU tests are ~10x slower to
# compile optimised, and the final gate (run_tests.sh) still builds them at -O1/-O2.
#   tests/run_one.sh <target> [target ...]
# This is not the final gate: the final gate is SANITIZE=1 tests/run_tests.sh.
set -e

cd "$(dirname "$0")/.."

OUT=build/host
CC=${CC:-cc}

usage() {
    cat <<'EOF'
usage: tests/run_one.sh <target> [target ...]

Targets for the edit loop (not the final gate):
  motion      motion, whole-step chance, ARP, song restore   tests/motion_test.c
  ui          UI against stub display / buttons / knobs      tests/ui_test.c
  ui_render   UI draw / layout lint                          tests/ui_render.c
  editor      editor protocol and MENU settings              tests/editor_test.c
  project     project formats and migration                  tests/project_test.c
  param       parameter contract                             tests/param_contract_test.c
  drums       ported DRUMS engine, every lane / KIT / choke  tests/drums_test.c
  sound15     ANALOG filter TYPE and ENV SYNC                tests/sound15_test.c
  inschain    insert chain order, MIX 0 bypass, migration    tests/inschain_test.c
  regress     golden renders, health, voices, CPU budget     tests/regress.c
  target_budget  render-loop instructions in the pi32v2 dis    tests/target_budget.py
  web         desc.json + menu.json, then node web/test_web.mjs
  all         exec the full gate: tests/run_tests.sh
  list        this list

The final gate is still SANITIZE=1 tests/run_tests.sh (or the full suite for 3+
binaries, see AGENT.md).
EOF
}

compile() { # compile <target> <output> <source> [flags...]
    c_target=$1
    c_out=$2
    c_src=$3
    shift 3
    echo "== $c_target: $CC -o $c_out $c_src $*"
    # source first, then the flags: GNU ld resolves -lm only after the object (as in run_tests.sh)
    $CC -o "$c_out" "$c_src" "$@"
}

run_bin() { # run_bin <target> <command> [args...]
    r_target=$1
    shift
    echo "== $r_target: $*"
    "$@"
}

do_motion() {
    compile motion "$OUT/motion_test" tests/motion_test.c \
        -w -Ibuild/gen -Ifirmware/src -lm || return 1
    run_bin motion "$OUT/motion_test"
}

do_ui() {
    # -O1 is paid back many times over: the 150000-frame SOAK and the stub loops run ~3x faster,
    # so build + run is ~18 s here against ~30 s at -O0.
    compile ui "$OUT/ui_test" tests/ui_test.c \
        -O1 -w -Ibuild/gen -Ifirmware/src -lm || return 1
    run_bin ui "$OUT/ui_test"
}

do_ui_render() {
    FV=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c)
    # -O1 is paid back many times over: the 220 screens x 9 palettes and the draw-cost loops run
    # ~3x faster, so build + run is ~10 s here against ~20 s at -O0.
    compile ui_render "$OUT/ui_render" tests/ui_render.c \
        -O1 -w "-DFELUCCA_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/src -Itests -lm || return 1
    mkdir -p build/ui_new/ppm build/ui_slot || return 1
    run_bin ui_render "$OUT/ui_render" build/ui_new build/ui_slot
}

do_editor() {
    compile editor "$OUT/editor_test" tests/editor_test.c \
        -w -Ibuild/gen -Ifirmware/src -lm || return 1
    run_bin editor env MENU_JSON="$OUT/menu.json" "$OUT/editor_test"
}

do_project() {
    compile project "$OUT/project_test" tests/project_test.c \
        -w -Ibuild/gen -Ifirmware/src -lm || return 1
    run_bin project "$OUT/project_test"
}

do_param() {
    if [ ! -f tests/param_contract_test.c ]; then
        echo "FAIL param: tests/param_contract_test.c is missing (the parameter contract test has not landed yet)" >&2
        return 1
    fi
    compile param "$OUT/param_contract_test" tests/param_contract_test.c \
        -w -Ibuild/gen -Ifirmware/src -lm || return 1
    run_bin param "$OUT/param_contract_test"
}

do_drums() {
    # compute-bound (renders every lane): -O2 as in run_tests.sh
    compile drums "$OUT/drums_test" tests/drums_test.c \
        -O2 -w -Ibuild/gen -Ifirmware/src -lm || return 1
    run_bin drums "$OUT/drums_test"
}

do_sound15() {
    compile sound15 "$OUT/sound15_test" tests/sound15_test.c \
        -O2 -w -Ibuild/gen -Ifirmware/src -lm || return 1
    mkdir -p build/sound15_demo || return 1
    run_bin sound15 "$OUT/sound15_test" build/sound15_demo
}

do_inschain() {
    compile inschain "$OUT/inschain_test" tests/inschain_test.c \
        -O2 -w -Ibuild/gen -Ifirmware/src -lm || return 1
    run_bin inschain "$OUT/inschain_test"
}

do_regress() {
    compile regress "$OUT/regress" tests/regress.c \
        -O2 -w -Ibuild/gen -Ifirmware/src -lm || return 1
    run_bin regress "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
}

do_target_budget() {
    if [ ! -f build/felucca.dis ]; then
        echo "== target_budget: skip (build/felucca.dis missing; run ./build.sh once)" >&2
        return 77
    fi
    run_bin target_budget python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt
}

do_web() {
    if ! command -v node >/dev/null 2>&1; then
        echo "== web: skip (node not found; this is not a test failure)" >&2
        return 77
    fi
    compile web "$OUT/descdump" tests/descdump.c \
        -w -Ibuild/gen -Ifirmware/src -lm || return 1
    echo "== web: $OUT/descdump > $OUT/desc.json"
    "$OUT/descdump" > "$OUT/desc.json" || return 1
    do_editor || return 1
    run_bin web node web/test_web.mjs
}

run_target() {
    case $1 in
        motion) do_motion ;;
        ui) do_ui ;;
        ui_render) do_ui_render ;;
        editor) do_editor ;;
        project) do_project ;;
        param) do_param ;;
        drums) do_drums ;;
        sound15) do_sound15 ;;
        inschain) do_inschain ;;
        regress) do_regress ;;
        target_budget) do_target_budget ;;
        web) do_web ;;
    esac
}

if [ "$#" -eq 0 ]; then
    usage >&2
    exit 2
fi

need_tables=0
for t in "$@"; do
    case $t in
        motion|ui|ui_render|editor|project|param|drums|sound15|inschain|regress|target_budget|web) need_tables=1 ;;
        all|list) ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
done

if [ "$need_tables" -eq 1 ] && [ ! -f build/gen/felucca_tables.h ]; then
    echo "tests/run_one.sh: build/gen/felucca_tables.h is missing: run ./build.sh once (or copy build/gen)" >&2
    exit 1
fi

mkdir -p "$OUT"

failure=0
for t in "$@"; do
    case $t in
        list)
            usage
            continue
            ;;
        all)
            echo "== all: exec tests/run_tests.sh"
            exec tests/run_tests.sh
            ;;
    esac
    status=0
    run_target "$t" || status=$?
    case $status in
        0) echo "PASS $t" ;;
        77) echo "SKIP $t" ;;
        *)
            echo "FAIL $t"
            failure=1
            ;;
    esac
done

[ "$failure" -eq 0 ] || exit 1
