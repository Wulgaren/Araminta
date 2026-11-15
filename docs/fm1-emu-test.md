<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments -->
# The device-timed emulator test (fm1-emu)

The host tests run the firmware's C on the computer's CPU. They show what the code does, not whether an FM-1 keeps
up with it. This test runs the real package, `build/felucca.fwsc`, on Joakim Eriksson's FM-1 emulator
([fm1-emu](https://github.com/joakimeriksson/fm1-emu), MIT) in its strict mode:

- each instruction costs what it cost on a real FM-1 (cycle probes run on the device, fm1-core `op_cost8`: ALU ops
  ~0.6 cycles, loads 1.25, multiplies 1.9, divides 6.75, taken branches 5-6.5);
- code and data read from flash go through a model of the 32 KB flash cache (64-byte lines, ~300 cycles a miss,
  also measured on the device);
- the audio DMA, TIMER5's interrupt nested in the render and both cores run as on the chip.

The firmware measures itself the same way on the emulator as on the device: `felucca_dbg` (audio.c) counts the
halves, the late ones and why, and keeps `cpu_q8`, the render's share of the 2.9 ms half-buffer, and `cpu1_q8`, the
second core's share (the TIMER4 time of its jobs: the parts, the send buses and the punch-in layer). `shed_count`
counts the voices the overload guard took.

## Running it

```sh
brew install rustup && rustup default stable     # once; rustup is keg-only:
export PATH=/opt/homebrew/opt/rustup/bin:$PATH    # (or put this in ~/.zshrc)
./build.sh
FM1_EMU=1 tests/run_tests.sh
```

`FM1_EMU=1` fetches fm1-emu at the pinned commit (`tools/get_fm1emu.sh`, into the gitignored `build/deps/fm1-emu`),
prints the firmware's ids for the runner (`tests/emu/ids.c`), builds the runner (`tests/emu`) and runs four
scenarios as separate parallel test units with `--clear`. The layout sweep shares boots within each layout:

| Scenario | What plays |
| --- | --- |
| `dry` | the heavy patch (FM6, PHASE, VOICE at full poly, DRUM on 16ths, every insert at MIX 100, both sends) |
| `keys` | the heavy patch, every audio punch-in key of both pages pressed and let go in turn |
| `stack` | the heavy patch, REPEAT 1/16 + LPF + HPF + FLANGER + PHASER + both THROWs + OCT UP latched together |
| `churn` | the heavy patch, seeded random punch-in presses and releases every 3-93 ms |
| `project` | `--project FILE`: that project's own four parts, kit and sequencer, played with `--notes N` held notes (default 4) on each part; no heavy patch and no punch page |

The patch goes in through the editor protocol over the emulated USB-MIDI, the chords and drum hits as MIDI notes, the
punch-ins as keys on the emulated panel. A scenario fails when the firmware faults, the setup is not taken, or a
punch-in does not engage or does not let go. Each prints one line of JSON: `cpu_pct` (the highest during the
scenario), `cpu1_pct` (the second core's highest, its jobs only), `audio_max_us`, `late` (halves the DMA moved past,
with `late_over` / `late_t5` / `late_oth` saying why), `wall_max_us`, `voices_shed`.

`project` takes a project's raw bytes (the backup's object 0, the running project) and PUTs them through the backup
protocol (`editor_backup.c`, cmd 67) before playing, so a reported patch is measured as it is instead of through a
synthetic one. It runs alone in a boot. `--notes` changes how many notes each part holds: the voice budget is eight
across all parts, so a busy kit plus held chords is what sheds.

One scenario by hand:

```sh
build/emu-target/release/emu_test --fwsc build/felucca.fwsc --elf build/felucca.elf --scenario stack --ms 6000 \
    [--clear] [--wav out.wav] [--fast] [--seed N] [--mhz 240] [--profile N] [--footprint N]
build/emu-target/release/emu_test --fwsc build/felucca.fwsc --elf build/felucca.elf --scenario project --ms 6000 \
    --project proj.bin [--notes N]
```

`--profile N` prints the N functions each core spent the most device time in during the scenario (from the ELF's
symbols): core 0 inside the audio ISR (outside it: `(main loop)`), core 1 at its jobs (waiting: `(idle)`). Inlined
code counts as its caller.

Several scenarios can share one boot and patch upload:

```sh
build/emu-target/release/emu_test --fwsc build/felucca.fwsc --elf build/felucca.elf \
    --scenarios 'dry:3000 keys:9000 churn:6000 stack:6000' --clear
```

The runner prints one JSON line per scenario and stops on the first failure. Between scenarios it releases keys
and MIDI notes, stops the transport, clears latched punch-ins, restores PUNCH-IN 1/2, sets FX LATCH for the next
scenario, restarts the chords and drums, then lets playback settle for one second. `--clear` applies that same
preparation before the first scenario too, including a single-scenario run. Each measurement starts with its own
counters and probes. The emulator and effect tails keep running, so this is not a cold-machine snapshot: compare
changes using the same scenario order and preparation, and establish a new baseline when switching from the old
separate-boot measurements. `--wav` and `--footprint` are available for a single scenario only: the first writes
one run's audio, the second flushes the flash cache every half, which would start a later scenario in the same boot
cold.

Focused checks for the runner and sweep:

```sh
python3 tests/layout_sweep_test.py
python3 tests/emu/runner_test.py    # real firmware and release runner must already be built
```

`--fast` (the JIT, a few seconds, not device-timed) also runs single-core: fm1-emu's loop skip fast-forwards core 0
through its wait for core 1's first job, the job times out and the firmware holds core 1 for the session. Profile
and measure in strict mode only.

The load is printed, not held to a bound, until the emulator is calibrated against a device (below).

## Calibrating against a real FM-1

The question is whether the emulator's `cpu_pct` is the device's. The cost model leaves out load-use stalls, and
the flash cache's 4-way LRU layout is assumed, so the two can differ by some factor. Measure it once:

1. Flash the same `build/felucca.fwsc` to the FM-1. Note which flash chip it has (MENU > ABOUT; fm1-emu needs
   `--flash-chip uc25hq80` for the newer one).
2. Set up the `dry` patch on the device as `emu_test` does (`heavy_patch` in `tests/emu/src/main.rs`): with the web
   editor, parts 1-4 get FM6, PHASE, VOICE and DRUM, preset 0; parts 1-3 POLY, SUS 127, ARP off; every part's
   insert MIX 100, DLY and REV 100; 120 BPM. From a DAW or MIDI file: hold notes 48 52 55 59 60 64 67 71 on channel
   1, the same +12 on channel 2, the first four +24 on channel 3, and on channel 4 a hit every 125 ms (36 on the
   beat, 38 on beats 2 and 4, 42 between, each ending the one before). Press PLAY.
3. After 30 s, turn on MENU > USB SERIAL, open the console (`screen /dev/cu.usbmodem*`), type `status` and note
   `cpu_pct`, `audio_max_us`, `audio_late` and `voices_shed`.
4. Run `emu_test --scenario dry --ms 30000` (strict) and note the same numbers.
5. The factor is the device's `cpu_pct` over the emulator's. Repeat with a second, different patch (drums and
   samples, no FM). If the two factors differ by more than ~10 %, keep the larger: a test that reads a little high
   is the safe side.
6. Record it below with the firmware commit and the date. Recalibrate when code moves between RAM and flash or the
   compiler flags change (both change what an instruction costs).

With a factor `F` within a few percent of 1, the emulator's numbers stand as they are. Otherwise `--mhz` runs the
emulated FM-1 at `240 / F` MHz: the firmware's own overload guard and late counters then act as they would on the
device.

## The render's hottest code in RAM (core.h AUDIO_RAM)

`--profile` showed ~950 flash cache misses a half (~1.2 ms of the 2.9 ms half): the render's code is ~46 KB, the
cache 32 KB, and every block evicts it again. RAMTEXT (app.ld) had ~21 KB free. In it now (`.hot_text`, ~17.6 KB):
the insert kernels (`ins_run_swept`, `_dist`, `_crush`, `_filter`, `_ott`), FM6 and VOICE (`fm6_render`,
`formant_render`), the punch-in layer's per-sample code (`perf_block`, `perf_harm`, `_flanger`, `_phaser`, `_glt`,
`_glt_seg`, `pb_ring_f`), and the phase engine (`phase_render`, with `pd_setup` and `pd_wave`). A call cannot reach
between RAM and the flash: those crossings go through `AUDIO_FAR`.

| Scenario | cpu_pct before | after | late halves before | after |
| --- | --- | --- | --- | --- |
| dry, 3 s | 103.9 % | 81.6 % | 491 | 8 |
| keys, 9 s | 115.6 % | 91.4 % | 1515 | 216 |
| churn, 6 s | 116.8 % | 89.1 % | 1006 | 61 |
| stack, 6 s | 134.4 % | 107.4 % | 1026 | 765 |

Placement moves the rest of the flash code too, and with it which lines share a cache set: judge a change by every
scenario's totals, not by one function's share. The phase engine is in RAM too (`phase_render`, `pd_setup`,
`pd_wave`: ~1.1 KB of `.hot_text`): against the table's after column, keys fell 91.4 -> 85.5 %, churn 89.1 ->
86.3 %, stack 107.4 -> 104.7 %, and dry stayed at 81.6 %. `pd_setup` and `pd_wave` are `noinline`, so their divides
and `switch` stay out of `phase_render`'s loop; with `pd_wave` inlined, `target_budget.py` read 8884 for that loop
against 77, and out of line it reads 77.

## The punch-in layer on the second core (2026-11-14)

The FX hold layer (perform.c) was the last large serial stage on core 0: after the parts, the buses and MASTER's
level, before master_out. It now runs one CTL block (0.73 ms) late: `perf_take` stages each block's mix (after CLIP
and MASTER's level) and `perf_begin`'s controls in one of two slots; `c1_layer` runs `perf_block` on the other slot's
block, on core 1, beside this core's tail (THROW, the sends, TAPE, the master chain); `perf_out` runs master_out on the
result a block later. The latency is constant, so the click and the HOME scope are one block early and the USB tap gets
the delayed mix. `perf_begin` (core 0, before the parts) takes and gives back the SLICER's buffer, so the job never
drops the slicer's state under a part. `cpu1_pct` is new: the second core's share of the half, its jobs' TIMER4 ticks,
smoothed as `cpu_pct`.

Medians (min - max) over 8 flash layouts (`tests/layout_sweep.sh`), before (`7f533fe`) and after:

| Scenario | cpu_pct before | after | late halves before | after |
| --- | --- | --- | --- | --- |
| dry, 3 s | 75.8 % (75.4 - 77.0) | 76.2 % (75.4 - 77.0) | 0 | 0 |
| keys, 9 s | 81.2 % (80.5 - 82.8) | 78.9 % (78.1 - 80.1) | 25 (16 - 37) | 7.5 (3 - 13) |
| churn, 6 s | 82.4 % (81.2 - 84.4) | 79.3 % (78.5 - 80.1) | 13 (6 - 19) | 1.5 (0 - 5) |
| stack, 6 s | 98.8 % (98.0 - 100.8) | 94.5 % (93.0 - 96.5) | 634 (622 - 640) | 393 (193 - 560) |

`cpu1_pct` after: ~53 % dry, ~56 % keys and churn, ~70 % stack. The sweep found a lost SELECT turn: on one layout
keys' turn to PUNCH-IN 2/2 never took. The punch map read SELECT twice in a pass when the first read had nothing,
and a detent the TIMER5 scan counted in between (the audio ISR can hold the main loop there for a render) went to the
second read, which dropped it. It reads SELECT once now (ui_input.c; tests/ui_test.c's late-detent check). One build
alone can read anywhere in these ranges: the
first single-build numbers for this change were stack 89.5 % / 59 late (its best layout) and 96.5 % / 560 (its worst).

Stack holds every audio punch-in at once; its late halves were all `late_over`. What is left there is the layer's own
job being longer than the tail it hides behind in the worst blocks, plus the serial head (events, the keys) and tail
(the master chain, master_out) that only core 0 runs.

## Flash placement and `tests/layout_sweep.sh`

About a third of each half is flash cache misses (~950 us of 2902): the render runs ~40 KB of flash code a block
through the 32 KB cache. Code moving by a few hundred bytes changes which hot lines share a set, and `cpu_pct` moves
with it: on stack, the same firmware plus an unused function of 12, 36 or 60 volatile stores measured 89.5, 94.5 and
89.8 %. `tests/layout_sweep.sh` builds the working tree with `FELUCCA_LAYOUT_PAD=0..7` (8 x n such stores,
perform.c, only in those builds), runs each scenario on each image and prints every layout and the median and range.
Judge a load change by those, not by one build. The layouts build at once, each into its own `build/sweep/pad<n>/build`
(`FELUCCA_OUT`, tools/build.py). Those directories are retained: unchanged compiler inputs reuse `felucca.o`, including
when only the linker script changed. Results are discarded before building, so a failed build cannot reuse old
numbers. Each layout runs all requested scenarios in one boot with `--clear`; layouts run in parallel. A successful
runner publishes `build/sweep/pad<n>/results.jsonl`, with its diagnostics in `run.log`. The summary rejects missing
or mismatched scenario results. The former separate-boot sweep took ~2.5 min on a 12-core Mac, stack alone ~1 min;
each of its 32 runs repeated ~9 s of device boot and setup time.

On Windows, one layout's four scenarios took 197.2 s with separate boots and 139.6 s with shared boot, a 29.2 %
reduction in reported runner wall time at equal concurrency (one emulator per comparison group). This is a
per-layout comparison, not a whole-sweep speedup estimate. A fresh layout build took 17.1 s versus 3.1 s with its
object retained, 81.7 % less build time. The cached build skipped compiling `felucca.o`. The full host gate keeps
its four emulator units parallel to avoid serializing that shorter check.

Shared-boot reference on Windows, firmware `226d3e3`, all eight layouts, in the default scenario order with
`--clear` (2026-11-14). These use the Windows firmware toolchain and the preparation above, so establish a local
reference again before comparing a change on another machine or toolchain:

| Scenario | cpu_pct median (min - max) | late halves median (min - max) |
| --- | --- | --- |
| dry | 76.2 % (75.4 - 77.0) | 0 (0 - 0) |
| keys | 80.1 % (78.9 - 81.2) | 9.5 (5 - 12) |
| churn | 80.3 % (79.3 - 80.9) | 2.5 (1 - 7) |
| stack | 94.9 % (93.4 - 96.5) | 441 (270 - 553) |

`--footprint N` shows what competes for the cache: the 64-byte lines the firmware fetches from the flash in a half
(code and data), in total and by symbol.

```sh
build/emu-target/release/emu_test --fwsc build/felucca.fwsc --elf build/felucca.elf --scenario stack --ms 4000 --footprint 40
```

It flushes the flash cache at each half boundary and counts the lines fetched in that half (code and flash data), per
symbol, against the cache's 512 lines. A line two symbols share counts for both. The run's `cpu_pct` and `late` are not
valid: every half starts cold, not warm.

## A fixed home for the render's flash code (2026-11-14)

`--footprint` put the render's hottest flash functions (`fm1_alnk0_irq` 85 lines a half, `fm1_timer5_irq` 39,
`dx_run` 32, `track_render_audio` 28, `rev_hall` 27) and the `SINE` table (33) through the 580 KB image. Their
addresses moved whenever unrelated code changed size, and the cache maps a 64-byte line to a set by its address:
that is why the same firmware scored 89.5-96.5 % on stack across 8 layouts.

`firmware/app.ld` now places the 94 functions `--footprint` ranked highest (41,476 B, 64-byte aligned, in the
listed order) directly after crt0, between `_flash_hot_start` and `_flash_hot_end`; `.rodata` follows them, so
`SINE` and the tables keep their sets too. `-ffunction-sections` gives each function its own `.text.<name>`, and
`tools/build.py` fails the build when a listed function is missing (renamed or inlined away) or lands outside the
block. A source-level section attribute was tried first and dropped: it keeps the compiler from inlining the
function into its caller.

Medians (min - max) over 8 layouts (`tests/layout_sweep.sh`), before (dev `226d3e3`) and after:

| Scenario | cpu_pct before | after | late halves before | after |
| --- | --- | --- | --- | --- |
| dry, 3 s | 76.4 % (75.4 - 77.0) | 73.0 % (72.7 - 76.6) | 0 | 0 |
| keys, 9 s | 78.9 % (78.1 - 80.1) | 76.6 % (76.2 - 76.6) | 7.5 (3 - 13) | 0 |
| churn, 6 s | 79.5 % (78.1 - 80.9) | 76.2 % (74.6 - 76.6) | 1.5 (0 - 3) | 0 |
| stack, 6 s | 94.5 % (93.4 - 96.5) | 87.1 % (86.7 - 87.5) | 393.5 (226 - 563) | 35 (31 - 41) |

The flash lines a half barely moved (stack, 4 s: 605.9, 37.9 KB, max 932 -> 592.9, 37.1 KB, max 940): `--footprint`
starts every half cold, so it counts a half's working set, not the warm-run set conflicts the fixed home removes.

With the punch-in recorder fix (`9c218ee`) merged, the same sweep reads dry 74.4 %, keys 76.6 %, churn 76.6 %,
stack 87.5 % (86.3 - 87.9), late 39 (33 - 44).

## Calibration record

| Date | Firmware | Patch | Device cpu_pct | Emulator cpu_pct | F |
| --- | --- | --- | --- | --- | --- |
| (none yet) | | | | | |

## First results (uncalibrated, 2026-11-14, `1e47815`)

| Scenario | cpu_pct | late halves | halves rendered | voices shed |
| --- | --- | --- | --- | --- |
| dry, 3 s | 103.9 % | 491 | 545 of ~1034 | 6 |
| keys, 9 s | 115.6 % | 1515 | 1587 of ~3100 | 18 |
| churn, 6 s | 116.8 % | 1006 | 1063 of ~2068 | 12 |
| stack, 6 s | 134.4 % | 1026 | 1043 of ~2068 | 12 |

The test patch (every insert at MIX 100 on four parts) is past the budget before any punch-in. A late half costs a
whole half: the ISR acknowledges the next one while still rendering, so it is skipped and the DMA replays stale
audio. Voice shedding stops after a handful (each part keeps its lowest note): the cost is in what does not scale
with voices, the parts' inserts, the sends, MASTER and the punch-ins. Core 1 waits 64 % of the time: it renders a
share of the parts, then core 0 alone runs the serial tail (bus join, punch-ins, reverb and delay, MASTER).
(An earlier run of this table had MIDI IN left at SEL, so every part's notes played on part 1.)

Against devices (not a calibration: other patches). Console logs from FM-1s on v2.1-v2.6 with users' projects show
the same regime and the same cause: `cpu_pct` 60-93 %, `audio_max_us` 3200-3945 against the 2902 us half, late
halves and shed voices in the thousands over an hour, and every late half `late_over` (the render itself), none
`late_t5` or `late_oth`, as in the emulator. fm1-emu's costs are measured per instruction class and for the cache
(its `probes/cycles.c`, `probes/cache.c`); a whole firmware's load has not been compared with a device yet.

The calibration build: `FELUCCA_CDC=1 FELUCCA_ICONS=0 FELUCCA_UART=0 ./build.sh` (the console back; the icons and
TRS MIDI out to fit). Its `dry` patch, 30 s, strict: `cpu_pct` 77.0, `audio_max_us` 2610, 0 late, 62 voices shed.
