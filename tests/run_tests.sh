#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Host tests of the Felucca sources (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
# TEST_JOBS=n: how many test units run at once (default: the CPUs, at most 10; TEST_JOBS=1 = one at a time).
# TEST_TIMEOUT=n: kill and fail a unit whose command runs longer than n seconds (default 600).
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, the GM map on DRUM, voice modes, FX sends, a 4-track mix: one hash
#                   each in tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             instructions / sample per preset and mix (tests/cpu_baseline.txt, +25 %), ns printed;
#                   target: loop instructions of the render functions in the pi32v2 disassembly
#                   (tests/target_budget.txt, +10 %; exact, static).
#   voices          the budget of 8, steal fades, MONO / LEGATO / UNISON keep their note, the VOICE cap,
#                   no hanging notes on any MIDI / key routing.
# ENGINE MEMORY (tests/union_test.c, 1.5): engine switches on a part (every pair and A -> B -> A of the engines that
#                   share the part's state region, with others), hashed against 1.4's (tests/union_golden.txt).
# UI renders (tests/ui_render.c): every screen in every palette from the real drawing code: the layout lint (no text
#                   off the screen, cut, hidden, overlapping or spilling out of its cell / card; only free text
#                   ellipsised), GREY gray, MONO neutral, the draw cost, the text audit (build/ui_new/text_audit.tsv);
#                   PNGs of GREY GREEN PAPER NIGHT HI-CON in build/ui_new (tests/ui_render.py), the findings in build/ui_new/report.txt;
#                   FM6's 32 algorithm charts as drawn (no box overlapping, no route through a box or crossing another), again
#                   on OPERATOR with each operator picked and its frequency text at its widest (1.2, #76);
#                   DIGITAL's screens (its algorithm charts, OP ENV) with FELUCCA_FM4=1 too (build/ui_fm4: lint, GREY, MONO);
#                   every frame of the rolling digits (lint, GREY, MONO), their filmstrips in build/ui_slot;
#                   the alignment: every text / icon / keycap meant to be centred, or on its neighbours' line, by its
#                   ink against its box (cells, chips, buttons, rows, knobs, the roll's strip, the keycaps' pills), every
#                   screen in FLAT and LINE and every palette, over every value it can show; 1 px off or more fails
#                   (build/ui_new/align.txt). MENU > LARGE: every screen again (FLAT, LINE, every palette) and the page / value
#                   sweep with the tall cards, the same lint and alignment; sheet_LARGE_GREY.png, sheet_LARGE_MONO.png.
# UI (tests/ui_test.c): the UI sources against stub display / buttons / knobs: sound loads keep the steps and
#                   the track's ARP / SCL / SLICER, the SAVE > PHRASES loader and its REPLACE? dialog, the
#                   one-step undo of both (SAVE held), REC on TRACKS / SEQ / ARP, STEP and ARP while recording,
#                   MIDI IN ROUT, saves refused while playing and the OVERWRITE? dialog, MUTE on TRACKS KNOB 1, FM6's operator
#                   pages (each byte alone, SLOT OWN, the sound, undo, the macros, projects / user presets),
#                   the DRUM grid (keys, knobs, LEDs, pages, live recording into it, BEAT from PATTERNS).
# Audio / persistence / editor: the real C paths against simulated DMA and NOR flash: bounded overload
#                   fades, shared-voice limits, deferred settings and retries, failed-save rollback, the autosave
#                   (1.2: when it writes, the power-on restore, damaged copies, a write cut short, wear over a session),
#                   malformed transfers, transport-stop timeouts, MIDI and UART recovery; the MENU settings over the
#                   editor (MENU_DESC / MENU_SET: every item, clamping, unknown ids, saving, USB SERIAL applied later).
# FM6 BANK (tests/fm6bank_test.c, 1.4.1): the voice bank (src/fm6_vbank.c) and its editor commands against simulated NOR:
#                   written whole and read back, its sectors (0xE7000, staged in the autosave's older copy, nothing else
#                   touched), a power cut after any programmed byte or in any erase (the old bank or the new one, the
#                   autosave kept), a wrong CRC, an autosave between requests, SLOT B1..B32 loading the exact records,
#                   empty bank slots skipped / refused, projects and user presets keeping a bank voice, stored SLOT values
#                   (projects, user presets, motion) loading as before.
# CHORD (tests/chord_test.c): the chord keys (src/chord.c): diatonic triads / sevenths of several scales and roots,
#                   the fixed shapes and voicings (at most 4 notes), names, MONO plays the root, a release ends
#                   exactly what its key / MIDI note started, recording, the ARP, MIDI IN, kits ignore CHRD.
# FUN10 (tests/fun10_test.c, 1.2): LFO 2 (SYNC one cycle per note value at several tempi and on the external clock, OFF bit
#                   for bit as before; TRIG NOTE / FREE; POL UNI 0..+1, the matrix's AMP), NUDGE (live recording keeps a
#                   note's time, QUANTIZE ON plays as before, OFF plays the nudges late and early once a pass, never twice
#                   with live notes, RATCH on its start, nudges 0 play the same either way, a tempo change of the external
#                   clock, SEQ > AUTOMATION's QUANTIZE and NUDGE rows, PRESETS moves notes in time), 128 motion records, FUN10 round trip, FUN9 /
#                   FUN8 / FUN7 as 1.1 / 1.0 wrote them (tests/old_pack.h) loading exactly, user presets of 99 / 103.
# RATCH (tests/ratchet_test.c): a step's ratchet (x1..x4): its parts in the sequencer (equal, gated, chords and drum
#                   hits whole, one chance roll, swing, no slide or tie out, STOP), FUN8 round trip and older projects x1,
#                   user preset patterns, SEQ > AUTOMATION's RATCH rows and the roll / grid drawing.
# STEP 1.5 (tests/step15_test.c, #178 #173): SEQ > STEP's KNOB 3 LEN on a note (clean TIEs written after it and taken
#                   back, stopping at the next note and at LEN, LEN 0 clears the note, chords, undo, TIME elsewhere, the DRUM grid untouched,
#                   the longer note played), a step's VEL as it plays (96 by default, ACC 127, drum hits) and
#                   SEQ > AUTOMATION's VEL rows (SHOW / HIDE, KNOB 4, EDIT, undo).
# DETAIL 1.5.1 (tests/detail_test.c, #199): SEQ > DETAIL (STEP -> DETAIL -> AUTOMATION), KNOB 1..4 the cursor step's
#                   CHANCE RATCH NUDGE VEL walked with PRESETS (NO NOTE on a REST / empty step), the same values as
#                   AUTOMATION's rows (gone at their default; EDIT there deletes one), EDIT resets, undo per gesture, the
#                   DRUM grid's keys picking steps (several held), a chord's ratchet, playback, STEP's marks.
# MOD (tests/mod_test.c): the modulation matrix: slots that do nothing are bit-identical, every source on each
#                   kind of destination, clamping, MIDI CC1 / CC11 / aftertouch routing, the cost of 4 active
#                   slots (at most +5 %), demos in build/mod_demo/.
# SOUND (tests/sound14_test.c; #132 #104 #148): the matrix's S&H / SLEW sources (a value per LFO cycle, the glide) and
#                   DEPTH destination (the LFO's gain; none = untouched), ANALOG WAVE SYNC / SUB (pitch, no DC, bounded,
#                   the band-limited restart), SPREAD (SPRD 0 / MONO / LEGATO bit for bit, a note hard left / right, the
#                   mono sum, UNISON, the SLICER's gate and the mute key on both sides, mono sends), the cost; demos in
#                   build/sound14_demo/.
# INSERT (tests/insert_test.c, 1.5; #78 #177): the per-track INSERT (src/fx.c track_insert): TYPE OFF and MIX 0 bit for
#                   bit (POLY, SPREAD), each TYPE at its corners on 3 and 8 voices (no DC, bounded, MIX 50 % halfway
#                   to the wet), the MIX glide and TYPE changes, MIX / TYPE as automation and locks, FUN10 and user
#                   presets of 109 and 104, the pages' labels, the cost per type; demos in build/insert_demo/.
# SOUND 1.5 (tests/sound15_test.c; #104 #175): ANALOG's filter TYPE (LP bit for bit, every other engine ignores it, BP / HP
#                   responses around the cutoff, RES, bounded at the corners), ENV SYNC (OFF bit for bit, the note values
#                   on the knob, attack / decay / release times at 60 / 120 / 173 BPM and on the external clock), the cost;
#                   demos in build/sound15_demo/ (analog_bp, analog_hp, env_sync).
# PERFORM (tests/perform_test.c): the FX hold layer (src/perform.c): 1/16 starts, stereo buffer effects, the
#                   too-long REPEAT (1/8, 1/12, REVERSE), the SLICER interplay, silent layer keys, idle bit-identical, cost; build/perform_demo/;
#                   OCT UP / DN (the harmonizer): pitch, stereo, clicks, the shimmer bounded, cost; FLANGER / PHASER (1.2): no DC,
#                   bounded, the sweep in time with the tempo, clean release, stacked with REPEAT / LPF / HPF, cost; the key
#                   map (the two fixed PUNCH-IN pages, a page flip, FX LATCH); build/fx_demo/; 1.5.1: the
#                   delay's dotted TIME (lengths, the 1/4D clamp, the knob order, free ms: the FM-1-style
#                   one-pole roll, a fast turn riding at its own speed), REV / ECHO THROW (the sends as at 127,
#                   the glide, ECHO's capped feedback bounded, the tails ringing out, on PUNCH-IN 2/2); build/throw_demo/.
# REVERB (tests/reverb_test.c): REVERB TYPE (src/fx.c): ROOM bit for bit as before, SPRING's decay against SIZE,
#                   its chirp (group delay rising with frequency), stability at the corners, level, a model change
#                   without a click, its cost against ROOM (+30 % at most); demos in build/fx_demo/. 1.2's HALL: SPRING
#                   bit for bit as before, decay against SIZE and DAMP, stability, exact silence after the tail (27
#                   settings and levels), no offset, level, stereo, echo density / ringing / flutter against ROOM, SPRING
#                   and COMB8 (8 combs + 4 allpasses, host only, for comparison), model changes both ways, cost; the
#                   listening set in build/reverb_demo/ (drums, pluck, piano: dry and each model at SIZE 40 / 90 / 127).
# SLICE (tests/slice_test.c): slice tables, AUTO onsets of a user-slot loop, reverse, keys, modes, the MAN slices
#                   (SLICES page) and their store in the slot (src/slice_store.c); the presets and the loop;
#                   demos in build/slice_demo/.
# SAMPLE (tests/sample_test.c, 1.5): PIANO (5 zones), CR78 kit omitted (USR), USR1..3 at 5..7; zones, splits, pitch over the
#                   keyboard (YIN), 0.75 s at a root, the aliases, GRAIN SRC 0; no SLICE BREAK in the ROM blob;
#                   1.2's PIANO HD user-slot file (gen_samples.py --user-slot: build/piano_hd/PIANO_HD), as old users may
#                   have installed it: valid in USR1 and bit for bit as SET 0.
# INPUT (tests/input_test.c): the key / button debounce of hal/fm1_input.h against the TIMER5 scan and bouncing
#                   contacts: a press within 2 scans (<= 2.3 ms), one note per bouncy press, no early or hanging
#                   release, stray samples ignored, fast repeats, the encoders' detents; the LED scan: lit LEDs every
#                   frame, dim ones a short pulse (the second line write) every frame, each only on its own column;
#                   the breath (#119): dark .. ~60 % of lit (DIM LO ~30 %), smooth, no dark run over ~10 ms near its peak;
#                   the power-on sweep (1.1, hal/fm1_led_anim.h): its length, every LED every frame at its level, the
#                   head left to right with its tail, the buttons, the end on the idle glow, the tick as before after;
#                   the idle animation (1.2, MENU > ANIM IDLE): endless, every LED every frame, the light there and back,
#                   the buttons breathing, a key down ends it before its next frame.
# CLICK (tests/click_test.c, 1.1): the metronome (src/click.c) and the count-in (src/seq.c) through audio.c: each beat at
#                   the first sample of its block, with a 1/16 track's steps 1 5 9 13 (internal and external clock, DIV 1/8
#                   and SWING too), the accent, the frequencies, length, LEVEL, MASTER, OFF / REC / ON, USB audio bit for bit
#                   without it; COUNT-IN 1 / 2 BARS (step 1 exactly N bars after PLAY, STOP cancels, PLAY again nothing,
#                   never with an external clock), notes in its last eighth onto step 1; build/click_demo/.
# FREE TAKE (tests/freetake_test.c, P2-A / SLOOP): empty project, REC armed while stopped, first note starts a take with
#                   no tempo; REC closes 1/2/4 bars and sets BPM; PLAY button / STOP drops; transport PLAY closes; 24 s auto-close;
#                   a note started while an earlier one is still held sets SF_SLIDE; releasing it while held
#                   records a return to the held pitch; a strummed chord keeps its STRUM sweep at the take's
#                   tempo and does not slide; a 1/16 strum on a 1-2 bar take closes at 1/32.
# USB audio (tests/uac_test.c): the UAC1 descriptors as a host parses them (with and without CDC; 44100 and
#                   48000 Hz), the ring and packetiser: 44.1 frames per packet, every frame in order (bit for bit),
#                   underrun / overrun, restart; SET_CUR; the 44.1 -> 48 kHz resampler (every frame against a
#                   double-precision one, response, SNR, cost); the stream at 48 kHz (47..49 frames per packet,
#                   fast and slow I2S clocks, switching rates mid-stream).
# web (web/test_web.mjs): the editor protocol against its mock device, whose tables must equal the
#                   firmware's (tests/descdump.c -> build/host/desc.json; the MENU settings: tests/editor_test.c -> build/host/menu.json),
#                   the package builder, the updater.
# Browser emulator (web/emu, when emcc is there): the firmware in WebAssembly (build/emu) boots, plays keys and MIDI,
#                   draws, lights its LEDs, keeps a save across instances, plays a song bit for bit as the same file
#                   built with cc (web/emu/native_check.c); the cost of 1 s of a heavy song against real time.
# PHYS (tests/phys_test.c): stability over the whole parameter and pitch range, the worst-case cost against
#                   the heaviest factory preset, demos in build/phys_demo/; tests/phys_ref.cpp compares the
#                   fixed-point models with DaisySP's float originals when DaisySP is there (DAISYSP=path).
# DRUM (tests/drum_test.c): the drum voices (src/drum_voice.c): pitch, decay, centroid and level against
#                   Felucca's targets, the controls' directions, no clipping, DC, retriggers, the hat choke, the
#                   kick on a small speaker; the DRUM engine (src/eng_drum.c): its key map, the 8 lanes together,
#                   one hit per lane, the choke between lanes; the cost per voice; demos in build/drum_demo/.
# DRUMS (tests/drums_test.c): the ported Felucca engine (src/eng_drums.c, src/drums_voice.c): every lane, KIT
#                   select, hat choke, 8 lanes together.
# NOISE (tests/noise_test.c): the engine (src/eng_noise.c): COLR's slope (white, pink, brown), the filter and the
#                   register clock following the key, META periodic at the key, no DC, no clipping at the
#                   corners, a note from silence the same twice, the cost per voice; demos in build/noise_demo/.
# DIGITAL -> FM6 (tests/fm4_test.c, built with FELUCCA_FM4=1): the retired four-operator engine against its conversion
#                   (src/fm4_convert.c): routes and carriers per algorithm, the presets' PTCH, and the sound (pitch,
#                   centroid, RMS envelope) of its presets and algorithms; demos in build/fm4_demo/. tests/digital_test.c
#                   (FELUCCA_FM4=1 too): DIGITAL's operator envelopes. Default builds have no DIGITAL (engine 1 reserved).
#                   tests/fm4_div0_test.c (UBSan, #61): the conversion without a divide that can be 0 gives the values the
#                   guarded form gave (every INDEX x FLT ENV, random sounds); a 0.9 project with DIGITAL ORGAN converts.
# FM6 (tests/fm6_test.c): the 6-operator FM engine (src/eng_fm6.c, src/fm6_core.c): the 32 algorithms' carriers, the
#                   operator envelopes (stages, rates, the voice ending), bit-stable notes, a click-free retrigger, no DC /
#                   clipping over the factory patches, the macros' directions, PTCH, pack / unpack and the SysEx
#                   layouts, the 6-voice cap, the cost per voice; demos in build/fm6_demo/.
# WAVETABLE (tests/wt_test.c): the engine (src/eng_wt.c): the two factory tables (16 frames, 512-sample base) and the
#                   mip levels, every preset at C2 / C4, no DC / clipping, a note from silence the same twice, A-POS /
#                   FDBK / B>A / LEVEL / RATIO, USR1..USR3's sine fallback, the cost per voice; demos in build/wt_demo/.
# ROBUST (tests/robust_test.c): damaged or crafted stored data and editor requests: a SLICE scan bounded by the slot,
#                   a slot that fails its check leaves no zone, engine numbers past the last refused, a retained older
#                   RAM project bounded, user preset patterns inside their fields, malformed requests never stop PLAY.
# SECOND CORE (tests/dualcore_test.c, tests/c1_host.h): fx.c mix_block's second core as a thread (hal/fm1_cpu1.h's
#                   job), rendering the parts handed to it at the same time as the main thread renders the others:
#                   4 parts of every preset, engine mixes, per-part state torture and this fork's chain / send EFFECTS
#                   (the shared SEND DSP and its driver, FLANG / CHOR lines' windows, the SLICER's shared clock, the
#                   matrix moving the shared send knobs and the DLY / REV bus settings, CHAIN owners) rendered on one
#                   core and on two with every split of the parts, the firmware's own (mix_split) included: bit for
#                   bit the same (tests/regress.c built with -DC1_HOST: the same golden renders, tests/golden.txt).
#                   ThreadSanitizer (a compiler with it): both passes again, tests/tsan_canary.c first; a race
#                   the runtime must report, so a silent runtime (Apple's missed the SEND clock race that GCC's
#                   and clang's on Linux caught with both sides instrumented) fails the suite instead of passing it.
# Sanitizers (ASan + UBSan, when the compiler has them; SANITIZE=0 skips): the stored-data and protocol tests again
#                   (loader, M-UPGRADE entry, editor, projects, backup, ROBUST) and three short fuzz runs with fixed
#                   seeds: tests/fuzz_ed.c (editor SysEx and raw USB-MIDI packets), tests/fuzz_proj.c (mutated project
#                   stores -> import -> restore -> render), tests/fuzz_smp.c (user sample slot headers -> scan -> SAMPLE /
#                   GRAIN / SLICE). Longer runs: build/host/asan/fuzz_ed 300000 7 (iterations, seed), the same for the others.
#                   UBSan leaves out the DSP's intended wraps (signed overflow, shifts) and the XIP rebase of a user zone's
#                   offset (bounds, object-size, pointer-overflow: correct on the device's flat flash, not in C's model).
# Change baseline entries only for reviewed, intentional differences in sound or cost;
# retain every unaffected golden / CPU / target entry. VERBOSE=1: every render.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
# ccache (optional, when installed): the suite builds ~80 binaries, and a rerun with unchanged sources
# reuses them. USE_CCACHE=0 turns it off. CC_BIN is the compiler without the cache prefix: the sanitizer
# lines below rebuild with their own flags.
CC_BIN="${CC%% *}"
CCACHE_BIN=
if [ "${USE_CCACHE:-1}" != 0 ] && command -v ccache >/dev/null 2>&1; then
    CC="ccache $CC"
    CCACHE_BIN=ccache
fi
# build <output> <source> [flags...]: compile to an object, then link. ccache caches only -c compiles, so
# every test binary goes through both steps; -lm stays a link flag.
build_with() {
    b_cc=$1
    b_out=$2
    b_src=$3
    shift 3
    b_compile=
    b_link=
    for b_a in "$@"; do
        case $b_a in
            -lm) b_link="$b_link $b_a" ;;
            *) b_compile="$b_compile $b_a" ;;
        esac
    done
    $b_cc -c $b_compile -o "$b_out.o" "$b_src" || return 1
    $b_cc $b_compile $b_link -o "$b_out" "$b_out.o"
}
build() { build_with "$CC" "$@"; }
fail=0
# TEST_TIMEOUT=<seconds>: a unit's command is killed after this and fails the suite (default 600).
run() {
    echo "== $1"; shift
    "$@" &
    _p=$!
    ( sleep "${TEST_TIMEOUT:-600}"; kill -9 "$_p" 2>/dev/null ) &
    _w=$!
    wait "$_p"; _rc=$?
    kill "$_w" 2>/dev/null || :
    wait "$_w" 2>/dev/null || :
    [ "$_rc" = 0 ] || fail=1
}

# The suite runs as units: one unit = a shell function that compiles its binary and runs it
# (the compile + run pairs below). Up to $TEST_JOBS units run at once (TEST_JOBS=1: one at a
# time = the debug mode), each one's output into build/host/logs.<pid>/<n>.log: one log
# directory per run, so a second run (or an orphaned one) cannot disturb this run's
# transcript. The logs print in script order as they complete, so the transcript reads the
# same as the serial suite's. A failing unit sets fail exactly as run() did; a compile that
# dies under set -e stops the suite at that unit as before. Units sharing output files
# (build/fx_demo) wait for each other. With units running together (TEST_JOBS > 1), JOBS=4
# caps the fan-out of regress.c's own render children (default 8) so the machine is not
# oversubscribed. build/host/logs.<pid>/times records each unit's start and end epoch
# seconds (which unit owns the wall).
for d in "$OUT"/logs.*; do   # sweep the log directories of dead runs
    [ -d "$d" ] || continue
    pid=${d##*.}
    kill -0 "$pid" 2>/dev/null || rm -rf "$d"
done
LOGS=$OUT/logs.$$
rm -rf "$LOGS"; mkdir -p "$LOGS"
if [ -z "${TEST_JOBS:-}" ]; then
    TEST_JOBS=$(sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
    if [ "$TEST_JOBS" -gt 10 ]; then TEST_JOBS=10; fi
fi
if [ "$TEST_JOBS" -lt 1 ]; then TEST_JOBS=1; fi
if [ "$TEST_JOBS" -gt 1 ]; then export JOBS=4; fi
n=0       # units queued so far (each one's log number)
f=0       # logs printed so far
jobs=     # "pid:log" of the units still running
running=0
trap flush EXIT

# flush: print the finished logs in order (the transcript keeps the serial suite's order)
flush() {
    while [ -f "$LOGS/$((f + 1)).state" ]; do
        f=$((f + 1))
        cat "$LOGS/$f.log"
    done
}

# note <text>: a line that used to echo straight to the transcript (the skip lines)
note() {
    n=$((n + 1))
    printf '%s\n' "$1" > "$LOGS/$n.log"
    echo "0:0" > "$LOGS/$n.state"
    flush
}

# abort <log> <status>: a unit died mid-body (a compile error): stop here like the serial
# suite did: the units before it finish and print, the later ones do not print
abort() {
    trap - EXIT
    for j in $jobs; do
        pid=${j%%:*}
        log=${j#*:}
        if [ "$log" -lt "$1" ]; then
            wait "$pid" 2>/dev/null || :
        else
            kill "$pid" 2>/dev/null || :
        fi
    done
    jobs=
    running=0
    while [ "$f" -lt "$1" ]; do f=$((f + 1)); cat "$LOGS/$f.log"; done
    exit "${2%%:*}"
}

# reap: collect the units that finished; "0:1" in a state file = one of its runs failed
reap() {
    left=
    for j in $jobs; do
        pid=${j%%:*}
        log=${j#*:}
        if [ -f "$LOGS/$log.state" ]; then
            wait "$pid" 2>/dev/null || :
            echo "$log $(date +%s) end" >> "$LOGS/times"
            case "$(cat "$LOGS/$log.state")" in
            0:0) ;;
            0:*) fail=1 ;;
            *) abort "$log" "$(cat "$LOGS/$log.state")" ;;
            esac
            running=$((running - 1))
        else
            left="$left $j"
        fi
    done
    jobs=$left
    flush
}

# wait_slot: block until fewer than $TEST_JOBS units are running
wait_slot() {
    while [ "$running" -ge "$TEST_JOBS" ]; do sleep 0.2; reap; done
}

# unit_after <logs> <function> [args]: queue one unit; the logs it names (earlier units whose
# output files it shares) must finish before it starts
unit_after() {
    waits=$1
    shift
    n=$((n + 1))
    wait_slot
    (
        fail=0
        trap 'echo "$?:$fail" > "$LOGS/$n.state"' EXIT
        for w in $waits; do
            while [ ! -f "$LOGS/$w.state" ]; do sleep 0.2; done
        done
        "$@"
    ) > "$LOGS/$n.log" 2>&1 &
    pid=$!
    jobs="$jobs $pid:$n"
    running=$((running + 1))
    echo "$n $(date +%s) start" >> "$LOGS/times"
    if [ "$TEST_JOBS" -eq 1 ]; then wait "$pid" 2>/dev/null || :; fi
    reap
}
unit() { unit_after "" "$@"; }

# drain: wait for every unit, then print what is left in order
drain() {
    for j in $jobs; do wait "${j%%:*}" 2>/dev/null || :; done
    reap
}

u_storage() {
    build "$OUT/storage_test" tests/storage_test.c
    run "flash storage (A/B, torn writes)" "$OUT/storage_test"
}
unit u_storage

u_upreset() {
    build "$OUT/upreset_test" tests/upreset_test.c
    run "user presets (UP_PUT parser, bank round trip, versions, PHYS DRUM and SAMPLE PERC -> DRUM, grid records, DIGITAL kept)" "$OUT/upreset_test"
}
unit u_upreset

u_input() {
    build "$OUT/input_test" tests/input_test.c
    run "keys and buttons: fast press, long release, bouncy contacts (one note each), glitches, encoders" "$OUT/input_test"
}
unit u_input

u_cpu1() {
    run "second core: protected restart and recovery after a rejected mailbox read" python3 tests/cpu1_test.py
}
unit u_cpu1

u_power() {
    build "$OUT/power_test" tests/power_test.c -O1 -w
    run "power: stock V15 trace, cold/warm starts, every transfer timeout and rail read-back faults" "$OUT/power_test"
}
unit u_power

u_midi_uart() {
    build "$OUT/midi_uart_test" tests/midi_uart_test.c
    run "TRS MIDI parser" "$OUT/midi_uart_test"
}
unit u_midi_uart

HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/src/core.h)
u_uac_cdc() {
    build "$OUT/uac_test" tests/uac_test.c -O2 -DT_CDC=1 -DHALF_FRAMES=$HALF -lm
    run "USB audio input: descriptors (with CDC), ring and packets, 44.1 -> 48 kHz resampler, 48 kHz stream" "$OUT/uac_test"
}
unit u_uac_cdc
u_uac_nocdc() {
    build "$OUT/uac_test_nocdc" tests/uac_test.c -O2 -DT_CDC=0 -DHALF_FRAMES=$HALF -lm
    run "USB audio input: descriptors (without CDC), ring and packets, resampler, 48 kHz stream" "$OUT/uac_test_nocdc"
}
unit u_uac_nocdc
# USB descriptor layouts (#67): CDC UAC LAYOUT CDC-presented 48K; layout 0 and the console left out = 1.0's bytes
# (+ the 48 kHz rate with 48K = 1, the default; 48K = 0: byte for byte; + the AC collection's audio streaming
# interface first, FELUCCA_UAC_AS_FIRST = 1, the default since 1.6)
# one unit (the iterations share the usb_desc_test binary)
u_usb_desc() {
    for v in 1.1.0.1.1 1.1.1.1.1 1.1.2.1.1 1.1.3.1.1 1.1.0.0.1 1.1.2.0.1 0.1.0.1.1 1.1.0.1.0 1.1.0.0.0 1.1.2.1.0 0.1.0.1.0 \
             1.0.0.1.1 1.0.1.1.1 1.0.0.0.1 0.0.0.1.1; do
        IFS=. read -r t_cdc t_uac t_lay t_on t_48 <<EOF
$v
EOF
        build "$OUT/usb_desc_test" tests/usb_desc_test.c -DT_CDC="$t_cdc" -DT_UAC="$t_uac" -DT_LAYOUT="$t_lay" -DT_ON="$t_on" -DT_48K="$t_48"
        run "USB descriptors: CDC $t_cdc (presented $t_on), UAC $t_uac (48 kHz $t_48), layout $t_lay" "$OUT/usb_desc_test"
    done
}
unit u_usb_desc
USB_DESC=$n
# FELUCCA_UAC_AS_FIRST=0 (#67): the AC collection of 1.0 .. 1.5 (MIDI first), 1.0's bytes; the parser model of the
# kernel AppleUSBAudio (macOS <= 15) stops before the audio streaming interface there
u_usb_desc_asf() {
    for v in 1.1.0.1.1 1.1.0.0.1 1.1.0.1.0 1.1.0.0.0 0.1.0.1.1; do
        IFS=. read -r t_cdc t_uac t_lay t_on t_48 <<EOF
$v
EOF
        build "$OUT/usb_desc_test" tests/usb_desc_test.c -DT_CDC="$t_cdc" -DT_UAC="$t_uac" -DT_LAYOUT="$t_lay" -DT_ON="$t_on" -DT_48K="$t_48" -DT_ASF=0
        run "USB descriptors (AC collection MIDI first): CDC $t_cdc (presented $t_on), UAC $t_uac (48 kHz $t_48)" \
            "$OUT/usb_desc_test"
    done
}
unit_after "$USB_DESC" u_usb_desc_asf   # both loops rebuild $OUT/usb_desc_test: not at the same time

[ -f build/felucca.fwsc ] || { note "run ./build.sh first"; abort "$n" 1; }

u_ota() {
    build "$OUT/ota_test" tests/ota_test.c -DOWN_PKG=1
    run "M-UPGRADE entry (own loader)" "$OUT/ota_test" build/felucca.fwsc
}
unit u_ota

head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
u_ldr() {
    build "$OUT/ldr_test" tests/ldr_test.c
    run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc
}
unit u_ldr

if [ -f build/gen/felucca_tables.h ]; then
    u_performance() {
        build "$OUT/performance_test" tests/performance_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "mixer caches and silent-track clocks (Melodee P2-F)" "$OUT/performance_test"
    }
    unit u_performance
    u_scale() {
        build "$OUT/scale_test" tests/scale_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
    }
    unit u_scale
    u_chord() {
        build "$OUT/chord_test" tests/chord_test.c -w -Ibuild/gen -Ifirmware/src -lm
        run "chord keys: diatonic and fixed chords, voicings, MONO root, releases, recording, ARP, MIDI IN, kits" "$OUT/chord_test"
    }
    unit u_chord
    u_speaker() {
        build "$OUT/speaker_test" tests/speaker_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "SPEAKER EQ: FLAT / LOWCUT / BASS+ responses, BASS+ harmonics of the bass, the sub cut, no offset after, headroom on a full-scale kick (#180)" "$OUT/speaker_test"
    }
    unit u_speaker
    # hostsim serves two runs and the tracks demo directory
    u_hostsim() {
        build "$OUT/hostsim" tests/hostsim.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
        mkdir -p build/tracks_demo
        run "TRACKS: 4-track pattern, live recording (lengths, swing), voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
    }
    unit u_hostsim
    u_project() {
        build "$OUT/project_test" tests/project_test.c -w -Ibuild/gen -Ifirmware/src -lm
        run "project formats (FUN1..FUN5 -> FUN6, the grid and song chain; DIGITAL tracks -> FM6, SAMPLE PERC -> DRUM, SLICE -> DRUM)" "$OUT/project_test"
    }
    unit u_project
    u_generation() {
        build "$OUT/generation_test" tests/generation_test.c -w -Ifirmware/src
        run "original Grids rhythm fixtures" "$OUT/generation_test"
    }
    unit u_generation
    u_motion() {
        build "$OUT/motion_test" tests/motion_test.c -O1 -w -Ibuild/gen -Ifirmware/src -lm
        run "motion, whole-step chance, FUN7 migration, song restore, ARP repeat and the 1.2 ARP modes" "$OUT/motion_test"
    }
    unit u_motion
    # The ids outside a track's p[]: PITCH (lock-only), MFX_* (FX motion targets),
    # EV_* (AUTOMATION rows), PG_* (CHORD cards), P_ALLOC's sweep and MOTION_ID.
    u_param_contract() {
        build "$OUT/param_contract_test" tests/param_contract_test.c -O1 -w -Ibuild/gen -Ifirmware/src -lm
        run "parameter contract: PITCH, MFX, EV, PG, P_ALLOC, MOTION_ID" "$OUT/param_contract_test"
    }
    unit u_param_contract
    u_ratchet() {
        build "$OUT/ratchet_test" tests/ratchet_test.c -O1 -w -Ibuild/gen -Ifirmware/src -lm
        run "RATCH: x1..x4 in a step (notes, chords, drum hits), gates, chance, swing, projects, user presets, AUTOMATION RATCH rows" "$OUT/ratchet_test"
    }
    unit u_ratchet
    u_step15() {
        build "$OUT/step15_test" tests/step15_test.c -O1 -w -Ibuild/gen -Ifirmware/src -lm
        run "1.5 STEP: a note's LEN (TIEs on KNOB 3), a step's VEL (playback, AUTOMATION's VEL rows)" "$OUT/step15_test"
    }
    unit u_step15
    u_detail() {
        build "$OUT/detail_test" tests/detail_test.c -O1 -w -Ibuild/gen -Ifirmware/src -lm
        run "1.5.1 SEQ > DETAIL: a step's CHANCE RATCH NUDGE VEL on KNOB 1..4 (walk, entries, undo, DRUM, chords, playback, marks)" "$OUT/detail_test"
    }
    unit u_detail
    u_fun10() {
        build "$OUT/fun10_test" tests/fun10_test.c -O1 -w -Ibuild/gen -Ifirmware/src -lm
        run "1.2 (FUN10): LFO 2 SYNC / TRIG / POL, NUDGE (record, QUANTIZE, play, controls), 128 motion records, FUN10 / FUN9 / FUN8 / FUN7, user presets" "$OUT/fun10_test"
    }
    unit u_fun10
    u_midi_control() {
        build "$OUT/midi_control_test" tests/midi_control_test.c -O1 -w -Ibuild/gen -Ifirmware/src -lm
        run "USB/TRS clock, bend, sustain, ownership and panic recovery" "$OUT/midi_control_test"
    }
    unit u_midi_control
    u_digital() {
        build "$OUT/digital_test" tests/digital_test.c -O1 -w -DFELUCCA_FM4=1 -Ibuild/gen -Ifirmware/src -lm
        run "DIGITAL (retired, built here with FELUCCA_FM4=1): operator envelopes/levels" "$OUT/digital_test"
    }
    unit u_digital
    u_fm4() {
        build "$OUT/fm4_test" tests/fm4_test.c -O2 -w -DFELUCCA_FM4=1 -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/fm4_demo
        run "DIGITAL -> FM6: the conversion against DIGITAL (FELUCCA_FM4=1): pitch, centroid, RMS envelope; demos" \
            "$OUT/fm4_test" build/fm4_demo
    }
    unit u_fm4
    u_fm4_div0() {
        build "$OUT/fm4_div0_test" tests/fm4_div0_test.c -O2 -w -fsanitize=integer-divide-by-zero -fno-sanitize-recover=all -Ibuild/gen -Ifirmware/src -lm
        run "DIGITAL -> FM6 without a divide by zero (#61): the same values as before, a 0.9 ORGAN project (UBSan)" \
            "$OUT/fm4_div0_test"
    }
    unit u_fm4_div0
    u_theme() {
        build "$OUT/theme_test" tests/theme_test.c -O1 -w -Ibuild/gen -Ifirmware/src -lm
        run "themes: contrast, text blending and font metrics" "$OUT/theme_test"
    }
    unit u_theme
    u_text_ref() {
        build "$OUT/text_ref_test" tests/text_ref_test.c -O1 -w -Ibuild/gen -Ifirmware/src -lm
        run "text: pens, kerning and pixels equal the reference renderer" "$OUT/text_ref_test"
    }
    unit u_text_ref
    u_text_spacing() {
        if python3 -c "import PIL" 2>/dev/null; then
            run "text spacing: glyph gaps against the font's own (16x supersampled), S M <= 0.25 px, L <= 0.5 px" \
                python3 tests/text_spacing_test.py build/gen/ui_fonts.h
        fi
    }
    unit u_text_spacing
    u_settings() {
        build "$OUT/settings_test" tests/settings_test.c -O1 -w -Ibuild/gen
        run "settings: PER1..PER4 migration, palette ids and preference preservation" "$OUT/settings_test"
    }
    unit u_settings
    u_ui_test() {
        build "$OUT/ui_test" tests/ui_test.c -w -Ibuild/gen -Ifirmware/src -lm
        run "UI: sounds keep steps, undo, recording, MIDI overflow, pending saves, panel recovery, drum grid, song chain, GREY gray" "$OUT/ui_test"
    }
    unit u_ui_test
    FV=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c)   # (the splash, ABOUT: the real version)
    # ui_render writes the report and PNGs of ui_render.py's own directories
    u_ui_render() {
        build "$OUT/ui_render" tests/ui_render.c -O1 -w "-DFELUCCA_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/src -Itests -lm
        mkdir -p build/ui_new/ppm build/ui_slot
        run "UI renders: layout lint (every screen and palette, every page, engine and column value), GREY gray, MONO neutral, alignment by ink (1 px fails), draw cost" \
            "$OUT/ui_render" build/ui_new build/ui_slot
        if python3 -c "import PIL" 2>/dev/null; then python3 tests/ui_render.py build/ui_new build/ui_slot; fi
    }
    unit u_ui_render
    u_ui_test_fm4() {
        build "$OUT/ui_test_fm4" tests/ui_test.c -w -DFELUCCA_FM4=1 -Ibuild/gen -Ifirmware/src -lm
        run "UI built with FELUCCA_FM4=1 (DIGITAL, kept in the tree): its OP pages, EDIT cycle, algorithm charts" "$OUT/ui_test_fm4"
    }
    unit u_ui_test_fm4
    u_ui_render_fm4() {
        build "$OUT/ui_render_fm4" tests/ui_render.c -O1 -w -DFELUCCA_FM4=1 "-DFELUCCA_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/src -Itests -lm
        mkdir -p build/ui_fm4/ppm build/ui_fm4_slot
        run "UI renders with FELUCCA_FM4=1: DIGITAL's screens (EDIT, OP ENV, the 8 algorithm charts), lint, GREY gray, MONO neutral" \
            "$OUT/ui_render_fm4" build/ui_fm4 build/ui_fm4_slot
    }
    unit u_ui_render_fm4
    u_audio() {
        build "$OUT/audio_test" tests/audio_test.c -w -Ibuild/gen -Ifirmware/src -lm
        run "audio: overload protection, bounded fades and DMA diagnostics" "$OUT/audio_test"
    }
    unit u_audio
    u_click() {
        build "$OUT/click_test" tests/click_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "metronome and count-in: beats sample for sample (internal and external clock), accent, level, MASTER, not in USB, count-in timing, notes onto step 1" "$OUT/click_test"
    }
    unit u_click
    u_rec_transport() {
        build "$OUT/rec_transport_test" tests/rec_transport_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "REC transport: panel taps, track changes, LED and click stop while USB audio records at 44.1 / 48 kHz" "$OUT/rec_transport_test"
    }
    unit u_rec_transport
    u_freetake() {
        build "$OUT/freetake_test" tests/freetake_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "free take: empty project, REC armed, first note starts take, REC closes loop/tempo, PLAY/STOP drops, 24 s auto-close, held-note slides and returns" "$OUT/freetake_test"
    }
    unit u_freetake
    u_persistence() {
        build "$OUT/persistence_test" tests/persistence_test.c -w -Ibuild/gen -Ifirmware/src -lm
        run "persistence: deferred settings, retry and failed-save rollback" "$OUT/persistence_test"
    }
    unit u_persistence
    u_backup() {
        build "$OUT/backup_test" tests/backup_test.c -w -Ibuild/gen -Ifirmware/src -lm
        run "full backup: CRC before writes, stale runtime, USB reset / timeout, malformed objects, older projects" "$OUT/backup_test"
    }
    unit u_backup
    u_fm6bank() {
        build "$OUT/fm6bank_test" tests/fm6bank_test.c -w -Ibuild/gen -Ifirmware/src -lm
        run "FM6 voice bank (1.4.1): write / read, power cuts, CRC, SLOT B1..B32, stored SLOT values as before" "$OUT/fm6bank_test"
    }
    unit u_fm6bank
    u_editor() {
        build "$OUT/editor_test" tests/editor_test.c -w -Ibuild/gen -Ifirmware/src -lm
        run "editor: real C protocol, malformed transfers, queue recovery, MENU settings (writes build/host/menu.json)" \
            env MENU_JSON="$OUT/menu.json" "$OUT/editor_test"
    }
    unit u_editor
    MENU_UNIT=$n    # the web tests compare against its menu.json
    u_robust() {
        build "$OUT/robust_test" tests/robust_test.c -w -Ibuild/gen -Ifirmware/src -lm
        run "robustness: crafted sample slots, engine numbers, retained old projects, preset patterns, malformed requests" \
            "$OUT/robust_test"
    }
    unit u_robust
    u_mod() {
        build "$OUT/mod_test" tests/mod_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/mod_demo
        run "modulation matrix: off = bit-identical, the math, MIDI CC1 / CC11 / aftertouch, cost, demos" "$OUT/mod_test" build/mod_demo
    }
    unit u_mod
    u_sound14() {
        build "$OUT/sound14_test" tests/sound14_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/sound14_demo
        run "sound items: matrix S&H / SLEW / DEPTH, ANALOG SYNC / SUB, SPREAD (bit for bit at 0, L / R, mono sum), cost, demos" \
            "$OUT/sound14_test" build/sound14_demo
    }
    unit u_sound14
    u_inschain() {
        build "$OUT/inschain_test" tests/inschain_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "insert chain (P1-C): order, MIX 0 bypass, 1.5 INSERT migration, FX page order" "$OUT/inschain_test"
    }
    unit u_inschain
    # legacy insert_test (single TYPE) not rebuilt until updated for the 6-stage chain
    u_sound15() {
        build "$OUT/sound15_test" tests/sound15_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/sound15_demo
        run "1.5 sound items: ANALOG filter TYPE LP / BP / HP (responses, LP and other engines bit for bit), ENV SYNC (times at several tempi), cost, demos" \
            "$OUT/sound15_test" build/sound15_demo
    }
    unit u_sound15
    u_slicer() {
        build "$OUT/slicer_test" tests/slicer_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/slicer_demo
        run "SLICER: no clicks, timing, sync with the sequencer, STUT, cost, demos" "$OUT/slicer_test" build/slicer_demo
    }
    unit u_slicer
    u_swing() {
        build "$OUT/swing_test" tests/swing_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "SWING: track + global at most 100, sequencer and SLICER step lengths, the SWG display" "$OUT/swing_test"
    }
    unit u_swing
    u_perform() {
        build "$OUT/perform_test" tests/perform_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/perform_demo build/fx_demo build/throw_demo
        run "FX layer effects: on the 1/16, stereo, too-long REPEAT, SLICER, silent keys, idle bit-identical, clicks, OCT UP / DN, FLANGER, PHASER, the key map, dotted TIME, REV / ECHO THROW, cost, demos" "$OUT/perform_test" build/perform_demo build/fx_demo build/throw_demo
    }
    unit u_perform
    PERF_UNIT=$n
    u_reverb() {
        build "$OUT/reverb_test" tests/reverb_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/reverb_demo
        run "REVERB TYPE: ROOM / SPRING bit-identical, SPRING/HALL/SHIMR/CLOUD decay / stability, model changes, cost, demos" "$OUT/reverb_test" build/fx_demo build/reverb_demo
    }
    unit_after "$PERF_UNIT" u_reverb    # both write build/fx_demo: not at the same time
    u_regress() {
        build "$OUT/regress" tests/regress.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "regression: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
    }
    unit u_regress
    # the second core (fx.c mix_block, hal/fm1_cpu1.h): a thread renders the parts handed to it (tests/c1_host.h)
    u_regress_c1() {
        build "$OUT/regress_c1" tests/regress.c -O2 -w -Ibuild/gen -Ifirmware/src -DC1_HOST -pthread -lm
        run "second core: the golden renders with a thread taking every split of the parts in turn" \
            "$OUT/regress_c1" tests/golden.txt tests/cpu_baseline.txt
    }
    unit u_regress_c1
    u_dualcore() {
        build "$OUT/dualcore_test" tests/dualcore_test.c -O2 -w -Ibuild/gen -Ifirmware/src -pthread -lm
        run "second core: 4 parts of every preset, mixes and the chain / send FX: bit for bit on one core and on two" \
            "$OUT/dualcore_test"
        run "second core: the same with every job cut after its first part (mix_block renders the rest: no dropout)" \
            env C1_STALL=1 "$OUT/dualcore_test"
        run "second core: rejected jobs fall back without replaying the previous audio" \
            env C1_REJECT=1 "$OUT/dualcore_test"
    }
    unit u_dualcore
    u_union() {
        build "$OUT/union_test" tests/union_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "engine memory (1.5): every switch between the engines with per-part state (270 sequences; retired SAMPLE/GRAIN/SLICE out)" \
            "$OUT/union_test" tests/union_golden.txt
    }
    unit u_union
    u_descdump() {
        build "$OUT/descdump" tests/descdump.c -w -Ibuild/gen -Ifirmware/src -lm
        echo "== parameter and engine tables as JSON (for the editor mock test)"
        "$OUT/descdump" > "$OUT/desc.json" || fail=1
    }
    unit u_descdump
    DESC_UNIT=$n    # the web tests compare against its desc.json
    u_phys() {
        build "$OUT/phys_test" tests/phys_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/phys_demo
        run "PHYS: stability C-1..G9 over the parameter corners, worst-case cost against PHASE WIRE, demos" "$OUT/phys_test" build/phys_demo
    }
    unit u_phys
    D=${DAISYSP:-vendor/DaisySP}/Source
    u_phys_ref() {
        if [ -d "$D/PhysicalModeling" ] && command -v c++ >/dev/null 2>&1; then
            $CC -O2 -w -Ibuild/gen -Ifirmware/src -Itests -c -o "$OUT/phys_fixed.o" tests/phys_fixed.c
            c++ -O2 -std=c++14 -w -I"$D" -I"$D/Utility" -o "$OUT/phys_ref" tests/phys_ref.cpp \
                "$D/PhysicalModeling/modalvoice.cpp" "$D/PhysicalModeling/resonator.cpp" "$D/PhysicalModeling/stringvoice.cpp" \
                "$D/PhysicalModeling/KarplusString.cpp" "$D/Filters/svf.cpp" "$D/Utility/dcblock.cpp" \
                "$D/Dynamics/crossfade.cpp" "$OUT/phys_fixed.o"
            run "PHYS: the fixed-point models against DaisySP's float originals (mode frequencies, decays, Svf)" "$OUT/phys_ref"
        else
            echo "== skip PHYS reference test (no DaisySP: set DAISYSP to a checkout)"
        fi
    }
    unit u_phys_ref
    u_drum() {
        build "$OUT/drum_test" tests/drum_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/drum_demo
        run "DRUM: voice targets, controls, no clipping, retrigger, hat choke, the kick on a small speaker, keys, 8 lanes, cost, demos" "$OUT/drum_test" build/drum_demo
    }
    unit u_drum
    u_drums() {
        build "$OUT/drums_test" tests/drums_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        run "DRUMS: every lane, KIT select, hat choke, 8 lanes together (the ported Felucca engine)" "$OUT/drums_test"
    }
    unit u_drums
    u_noise() {
        build "$OUT/noise_test" tests/noise_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/noise_demo
        run "NOISE: colour slopes, key-tracked filter and clock, META period, DC, clipping, retrigger, cost, demos" "$OUT/noise_test" build/noise_demo
    }
    unit u_noise
    u_fm6() {
        build "$OUT/fm6_test" tests/fm6_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/fm6_demo
        run "FM6: algorithms, envelopes, retrigger, DC, clipping, macros, patch formats, voices, cost, demos" "$OUT/fm6_test" build/fm6_demo
    }
    unit u_fm6
    u_wt_test() {
        build "$OUT/wt_test" tests/wt_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
        mkdir -p build/wt_demo
        run "WAVETABLE: table/mips, FDBK, B>A, presets, DC, clipping, retrigger, cost, demos" "$OUT/wt_test" build/wt_demo
    }
    unit u_wt_test
    note "== skip SLICE (engine retired)"
    u_sample() {
        # SAMPLE's PIANO (1.5: as 1.1.5); CR78 factory kit at SET 8; 1.2's PIANO HD in a user slot
        if grep -q '^    {"PIANO", 0, 5},' build/gen/felucca_samples.h; then
            mkdir -p build/sample_demo build/piano_hd
            python3 tools/gen_samples.py --user-slot PIANO "PIANO HD" build/piano_hd/PIANO_HD >/dev/null
            build "$OUT/sample_test" tests/sample_test.c -O2 -w -Ibuild/gen -Ifirmware/src -lm
            run "SAMPLE: PIANO (5 zones as 1.1.5: splits, pitch, length, GRAIN), CR78 (SET 8, lanes), a PIANO HD slot" "$OUT/sample_test" \
                build/piano_hd/PIANO_HD build/sample_demo
        else
            echo "== skip SAMPLE's PIANO (no CC0 library: assets/samples-cc0, tools/fetch_cc0.py)"
        fi
    }
    unit u_sample
else
    note "== skip hostsim (run ./build.sh once)"
fi

# ASan + UBSan: the stored-data and protocol paths, and short fuzz runs (fixed seeds: the same inputs every run)
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize=signed-integer-overflow,shift,bounds,object-size,pointer-overflow"
SAN="$SAN -fno-sanitize-recover=undefined -w"
san_ok() {
    printf 'int main(void){return 0;}\n' > "$OUT/san_probe.c" &&
        build_with "$CCACHE_BIN $CC_BIN $SAN" "$OUT/san_probe" "$OUT/san_probe.c" 2>/dev/null && "$OUT/san_probe"
}
if [ "${SANITIZE:-1}" = 0 ]; then
    note "== skip the sanitizer runs (SANITIZE=0)"
elif [ ! -f build/gen/felucca_tables.h ]; then
    note "== skip the sanitizer runs (run ./build.sh once)"
elif ! san_ok; then
    note "== skip the sanitizer runs (the compiler has no ASan / UBSan)"
else
    mkdir -p "$OUT/asan"
    A="$OUT/asan"
    SCC="$CCACHE_BIN $CC_BIN $SAN -Ibuild/gen -Ifirmware/src"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    u_asan_ldr() {
        build_with "$SCC" "$A/ldr_test" tests/ldr_test.c
        run "ASan/UBSan: update loader (other app -> this build)" "$A/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc
    }
    unit u_asan_ldr
    u_asan_ota() {
        build_with "$SCC" "$A/ota_test" tests/ota_test.c -DOWN_PKG=1
        run "ASan/UBSan: M-UPGRADE entry (own loader)" "$A/ota_test" build/felucca.fwsc
    }
    unit u_asan_ota
    u_asan_t() {
        build_with "$SCC" "$A/$1" tests/$1.c -lm
        run "ASan/UBSan: $1" "$A/$1"
    }
    for t in editor_test project_test backup_test robust_test fun10_test fm6bank_test; do
        unit u_asan_t "$t"
    done
    u_asan_fuzz_ed() {
        build_with "$SCC" "$A/fuzz_ed" tests/fuzz_ed.c -lm
        run "ASan/UBSan fuzz: editor SysEx and raw USB-MIDI packets (20000, seed 7)" "$A/fuzz_ed" 20000 7
    }
    unit u_asan_fuzz_ed
    u_asan_fuzz_proj() {
        build_with "$SCC" "$A/fuzz_proj" tests/fuzz_proj.c -lm
        run "ASan/UBSan fuzz: project stores -> import -> restore -> render (5000, seed 13)" "$A/fuzz_proj" 5000 13
    }
    unit u_asan_fuzz_proj
    u_asan_fuzz_smp() {
        build_with "$SCC" "$A/fuzz_smp" tests/fuzz_smp.c -lm
        run "ASan/UBSan fuzz: user sample slot headers -> scan -> SAMPLE / GRAIN (1000, seed 17)" "$A/fuzz_smp" 1000 17
    }
    unit u_asan_fuzz_smp
    TSAN_CC="$CCACHE_BIN $CC_BIN -O1 -g -w -Ibuild/gen -Ifirmware/src -fsanitize=thread -pthread"
    u_asan_tsan_cc() {
        if build_with "$TSAN_CC" "$OUT/dualcore_tsan" tests/dualcore_test.c -lm 2>/dev/null; then
            : > "$LOGS/tsan.ok"
        else
            echo "== skip the ThreadSanitizer run (the compiler has no TSAN)"
        fi
    }
    unit u_asan_tsan_cc
    TSAN_BUILD=$n
    u_asan_canary() {
        [ -f "$LOGS/tsan.ok" ] || return 0
        build_with "$TSAN_CC" "$OUT/tsan_canary" tests/tsan_canary.c
        echo "== ThreadSanitizer canary: the runtime must report the race it is handed (else the runs below prove nothing)"
        "$OUT/tsan_canary" parent > "$LOGS/canary_parent.txt" 2>&1 || :
        "$OUT/tsan_canary" fork > "$LOGS/canary_fork.txt" 2>&1 || :
        canary=1
        for mode in parent fork; do
            if grep -q "ThreadSanitizer: data race" "$LOGS/canary_$mode.txt"; then
                echo "canary $mode: $(grep -m1 "ThreadSanitizer: data race" "$LOGS/canary_$mode.txt")"
            else
                echo "canary $mode: no report (the runtime is not looking)   FAIL"
                canary=0
            fi
        done
        [ "$canary" = 1 ] || fail=1
    }
    unit_after "$TSAN_BUILD" u_asan_canary
    u_asan_tsan1() {
        [ -f "$LOGS/tsan.ok" ] || return 0
        run "second core under ThreadSanitizer: 4 parts of every preset, mixes and the chain / send FX" \
            env TSAN_OPTIONS=halt_on_error=1 "$OUT/dualcore_tsan"
    }
    unit_after "$TSAN_BUILD" u_asan_tsan1
    u_asan_tsan2() {
        [ -f "$LOGS/tsan.ok" ] || return 0
        run "second core under ThreadSanitizer: the same with every job cut after its first part" \
            env C1_STALL=1 TSAN_OPTIONS=halt_on_error=1 "$OUT/dualcore_tsan"
    }
    unit_after "$TSAN_BUILD" u_asan_tsan2
fi

u_target_budget() {
    run "regression: target cost of the render loops (pi32v2 disassembly)" python3 tests/target_budget.py \
        build/felucca.dis tests/target_budget.txt
}
unit u_target_budget

u_install() {
    run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
}
unit u_install

u_web() { run "$1" node "$2"; }
if command -v node >/dev/null 2>&1; then
    # the web tests wait for the units above: the mock's tables are checked against their
    # build/host/desc.json and menu.json
    unit_after "$DESC_UNIT $MENU_UNIT" u_web "web pages: editor protocol + samples, package builder, update protocol" web/test_web.mjs
    unit_after "$DESC_UNIT $MENU_UNIT" u_web "web backup: capture, validation before writes, restore order" web/test_backup.mjs
    for t in test_crc test_info_tags test_installer test_steps_dom test_stock_sha; do
        unit_after "$DESC_UNIT $MENU_UNIT" u_web "web: $t" "web/$t.mjs"
    done
else
    note "== skip web tests (no node)"
fi

u_stock_sha() {
    run "stock firmware digest: the Python installer and the web validator agree" python3 tools/check_stock_sha.py
}
unit u_stock_sha

# the browser emulator: web/emu/build.sh writes build/emu, the native check writes
# $OUT/emu_native.f32 (independent), and the test reads both
u_emu_build() {
    run "browser emulator: the firmware to WebAssembly (web/emu/build.sh -> build/emu)" sh web/emu/build.sh
}
u_emu_native() {
    build "$OUT/emu_native" web/emu/native_check.c -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -lm
    run "browser emulator: the same song from the native build" "$OUT/emu_native" "$OUT/emu_native.f32"
}
u_emu_test() {
    run "browser emulator: boot, keys, MIDI, screen, LEDs, PLAY, a save kept across instances, = native, cost" \
        node web/emu/emu_test.mjs build/emu/felucca.wasm "$OUT/emu_native.f32"
}
if ! command -v emcc >/dev/null 2>&1; then
    note "== skip the browser emulator (no emcc: Emscripten builds web/emu)"
elif ! command -v node >/dev/null 2>&1 || [ ! -f build/gen/felucca_tables.h ]; then
    note "== skip the browser emulator (needs node and build/gen)"
else
    unit u_emu_build
    EMU_BUILD=$n
    unit u_emu_native
    EMU_NATIVE=$n
    unit_after "$EMU_BUILD $EMU_NATIVE" u_emu_test
fi

drain

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
