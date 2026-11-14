# Agent notes (Araminta)

Project rules for coding agents. Global workflow still lives in `~/AGENT.md`.

## Build

`./build.sh` builds the firmware. It works on this machine: the JieLi toolchain is installed
at `~/.jieli/toolchain` (symlink to the versioned directory) and the SDK files at
`build/deps/ac79`. On macOS the toolchain is x86-64 Linux, so `build.sh` runs each tool in a
`linux/amd64` `debian:bookworm-slim` container through Docker (Rosetta on Apple silicon):
Docker must be running, and on this machine it is `colima` (`colima start` if stopped).

If you need to know whether you can build, run `./build.sh`. It prints which piece is missing,
and that message is the authority. Do not conclude the toolchain is unavailable from a file
search: the Linux binaries are `clang` and `ld` with no `.exe`, and `build.sh` defaults
`JIELI_TOOLCHAIN` to `~/.jieli/toolchain`, so empty `PI32*`/`JL*` environment variables mean
nothing. `BUILDING.md` § Prerequisites has the full list (Pillow and fontTools are needed
too, for `build/gen`).

`build/felucca.fwsc`, `build/felucca.dis` and `build/loader/` are outputs; `tests/run_tests.sh`
needs them, so build once before assuming a suite failure is real.

## Host tests

`tests/run_tests.sh` is the full gate. It takes several minutes. It does **not** filter on argv: `tests/run_tests.sh sound15` still runs everything.

### Iterate

1. After `./build.sh` once (needs `build/gen/`).
2. Run only the target that covers the change: `tests/run_one.sh <target>`.
3. Done when that target exits 0 on the behaviour you touched.

`tests/run_one.sh` is the edit loop, not the final gate. It compiles and runs
one binary (or the web pair) from the same sources as `tests/run_tests.sh`, at
`-O0` where that is faster (`ui` and `ui_render` keep `-O1`, the compute-bound
engine targets `-O2`):

```
tests/run_one.sh list            # target list
tests/run_one.sh motion          # motion / ARP / song
tests/run_one.sh ui ui_render    # UI stubs plus draw / layout lint
tests/run_one.sh editor          # writes build/host/menu.json
tests/run_one.sh project
tests/run_one.sh param           # tests/param_contract_test.c
tests/run_one.sh sound15 inschain         # ANALOG filter / insert chain
tests/run_one.sh regress         # golden renders, health, voices, CPU budget
tests/run_one.sh web             # desc.json + menu.json, then node web/test_web.mjs
```

`tests/run_one.sh list` prints every target (`all` execs `tests/run_tests.sh`).

If a test has no target (`dualcore_test`, say), compile and run it by hand from
its `$CC` line in `tests/run_tests.sh`, without the `-O` level while iterating:
the single-TU tests compile ~10x faster unoptimised.

Web-only edits: `tests/run_one.sh web` (or `node web/test_web.mjs`; add
`web/test_backup.mjs` if backup paths moved).

If you must invoke the full script while iterating, use `SANITIZE=0 tests/run_tests.sh` so ASan/fuzz are skipped. Emulator tests already skip when `emcc` is missing.

The final gate is `tests/run_tests.sh` before the final push, with `SANITIZE`
set by the rule in Before the final push, and the full gate instead of the
targeted targets when 3+ binaries are touched (see Handoff below).

### Handoff

Gate by blast radius of the edit, not the full script by default.

1. List the distinct `build/host/*` binaries that cover the touched paths (use the map below; `ui_test` and `ui_render` count as two).
2. If that list has **1 or 2** binaries: compile and run each. If `web/` also moved, run the matching `node web/test_*.mjs` too. Docs-only / no behaviour change: no suite.
3. If the list has **3 or more** binaries: run full `tests/run_tests.sh`, with `SANITIZE` set by the rule in Before the final push.
4. Done when every required command exits 0 (full suite: `ALL HOST TESTS PASSED`).

Firmware + web in one change: run the host side from steps 2–3 **and** the web tests; do not skip one because the other passed.

Full suite on request, or before a commit/PR the user asks for, still wins over this rule when they ask for it.

### Before the final push

Run the full gate **once**. `SANITIZE=1` (the default) adds ASan/UBSan, three fuzzers and TSan on the
second core (`tests/run_tests.sh`, the block after `SAN=`). Use it when the change touches any row below,
else `SANITIZE=0`:

| Area | What the passes check | Main files |
| --- | --- | --- |
| Stored data and import | ASan/UBSan on project, backup, preset and flash tests; `fuzz_proj` | `project.c`, `storage.c`, `storage_hw.c`, `upreset.c`, `up_fm6.c`, `fm6_vbank.c`, `settings_persist.c`, `editor_backup.c`, `tests/old_pack.h` |
| Editor protocol and MIDI input | `editor_test`, `robust_test`, `fm6bank_test`; `fuzz_ed` | `editor*.c`, `usb.c`, `midi_uart.c`, `midi_control.c` |
| Update path | `ldr_test`, `ota_test` | `ota.c`, `ota_hw.c`, `firmware/loader/` |
| User sample slots | `fuzz_smp` | `eng_sample.c`, `eng_grain.c`, `slicer.c` |
| Signed audio math | strict UBSan tests, `punch_crackle_test` among them; the rule: `docs/signed-audio-arithmetic.md` | `eng_phys.c`, `phys_dsp.c`, `phys_symp.c`, `chord_melody.c`, `chord_auto.c`, `perform.c`, `pfx.c`, `fx.c`, `eng_drum.c`, `drumx_voice.c`; any fixed-point scaling or shift change |
| Second core | `dualcore_test` under TSan, the TSan canary | `fx.c`, `pfx.c`, `master.c`, `voice.c`, `engines.c`, `hal/fm1_cpu1.h`, `tests/c1_host.h`, `tests/dualcore_test.c`, `tests/tsan_canary.c`; an engine change that adds or changes a global or `static` the render touches |
| Any file | memory | a new buffer, array index, `memcpy` / `memset` size, or pointer arithmetic |
| The sanitizer runs | | a test the sanitizer block builds, or its `SAN` / `STRICT_CC` / `TSAN_CC` lines |

UI text and layout, icons, labels, docs, `web/` and Python tools take `SANITIZE=0`. Unsure: `SANITIZE=1`.
CI runs the sanitizers on every PR, so a wrong skip still fails there before merge.

Device load: add `FM1_EMU=1` to that run when the change can move what the render costs on an FM-1: the audio ISR
(`audio.c`), an engine, `voice.c`, `fx.c`, `perform.c`, `pfx.c`, `master.c`, where code sits (RAM or flash: `.pool`,
section attributes, the linker script), or `tools/build.py`'s compiler flags; and before a release. It runs the
package on fm1-emu timed as the device (`docs/fm1-emu-test.md`), locally only (it needs `cargo`; CI has no emulator).
Report each scenario's `cpu_pct`, `late` and `voices_shed` beside the doc's last recorded results: a rise is the finding.
While iterating on load, run one scenario, strict:
`build/emu-target/release/emu_test --fwsc build/felucca.fwsc --elf build/felucca.elf --scenario stack --ms 4000`
(~20 s; `--profile 40` shows where each core's time goes), and all four only for the final numbers. `--fast` runs
single-core: never measure load with it.

Do not also run the suite a second time with the other setting, and do not push until it prints
`ALL HOST TESTS PASSED`.

## Rough map (source → host binary)

| Touch | Binary under `build/host/` |
| --- | --- |
| `firmware/src/ui*.c`, help, menus | `ui_test` (also `ui_render` for draw/layout) |
| DRUM-X engine (`eng_drum.c`, `drumx_voice.c`) | `drum_test` (also `regress` for presets/CPU, `project_test` for the kit's storage and DRUMS migration) |
| ANALOG filter / ENV SYNC / sound 1.5 | `sound15_test` |
| insert chain / FX order | `inschain_test` |
| every FX stage in CHAIN / SEND / MASTER | `fx_route_test` |
| motion / ARP / song | `motion_test` |
| locks / special ids / PITCH / FX motion | `param_contract_test` |
| projects / FUN migration | `project_test` |
| editor protocol / MENU | `editor_test` |
| golden / CPU / voice budgets | `regress` |
