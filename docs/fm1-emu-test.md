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
halves, the late ones and why, and keeps `cpu_q8`, the render's share of the 2.9 ms half-buffer. `shed_count` counts
the voices the overload guard took.

## Running it

```sh
brew install rustup && rustup default stable     # once; rustup is keg-only:
export PATH=/opt/homebrew/opt/rustup/bin:$PATH    # (or put this in ~/.zshrc)
./build.sh
FM1_EMU=1 tests/run_tests.sh
```

`FM1_EMU=1` fetches fm1-emu at the pinned commit (`tools/get_fm1emu.sh`, into the gitignored `build/deps/fm1-emu`),
prints the firmware's ids for the runner (`tests/emu/ids.c`), builds the runner (`tests/emu`) and runs four
scenarios, each in its own unit (about 20-30 s each):

| Scenario | What plays |
| --- | --- |
| `dry` | the heavy patch (FM6, PHASE, VOICE at full poly, DRUM on 16ths, every insert at MIX 100, both sends) |
| `keys` | the heavy patch, every audio punch-in key of both pages pressed and let go in turn |
| `stack` | the heavy patch, REPEAT 1/16 + LPF + HPF + FLANGER + PHASER + both THROWs + OCT UP latched together |
| `churn` | the heavy patch, seeded random punch-in presses and releases every 3-93 ms |

The patch goes in through the editor protocol over the emulated USB-MIDI, the chords and drum hits as MIDI notes, the
punch-ins as keys on the emulated panel. A scenario fails when the firmware faults, the setup is not taken, or a
punch-in does not engage or does not let go. Each prints one line of JSON: `cpu_pct` (the highest during the
scenario), `audio_max_us`, `late` (halves the DMA moved past, with `late_over` / `late_t5` / `late_oth` saying why),
`wall_max_us`, `voices_shed`.

One scenario by hand:

```sh
build/emu-target/release/emu_test --fwsc build/felucca.fwsc --elf build/felucca.elf --scenario stack --ms 6000 \
    [--wav out.wav] [--fast] [--seed N] [--mhz 240] [--profile N]
```

`--profile N` prints the N functions each core spent the most device time in during the scenario (from the ELF's
symbols): core 0 inside the audio ISR (outside it: `(main loop)`), core 1 at its jobs (waiting: `(idle)`). Inlined
code counts as its caller.

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
