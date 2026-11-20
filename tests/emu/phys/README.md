# The PHYS sweep (device-timed emulator)

Three project stores for `emu_test --scenario project`, the ones the PHYS / RAM-layout work was measured
on. Each is a raw `project_store_t` (4864 B), the same shape as `tests/emu/busy_project.bin`.

| store | what it is |
|---|---|
| `busy_phys.bin` | 120 BPM, all four tracks on PHYS (engine 9), both sends at 100, the master TAPE effect on: the heaviest PHYS-only load |
| `psy_phys.bin` | PSYCHODELIA (70 BPM, SWING 71), all four tracks on PHYS, a delay and reverb per track, no punch lane |
| `psy_stack_p1_lfo.bin` | PSYCHODELIA with a mixed stack: PHYS, ANALOG, FM6, WAVETABLE, plus the page-1 punch lane `1:F3,1:A3,1:C4,1:D4,1:E4,1:F4,1:E5,1:F5` latched |

`busy_phys.bin` came from `busy_project.bin` with the four engines moved to PHYS; the two PSYCHODELIA
stores were made from that song's own project store with the engines and a few sound parameters changed.

## Running

```sh
./build.sh                                        # build/felucca.fwsc, build/felucca.elf
FM1_EMU=1 tests/run_tests.sh                      # builds the runner in build/emu-target
tools/phys_sweep.sh                               # three JSON lines
```

The runner's load is printed (cpu % per core, flash cache misses a half, the worst audio half in us, late
halves, voices shed) and not held to a bound; see `docs/fm1-emu-test.md`.

## Numbers these were measured with

JieLi's toolchain, 240 MHz, strict mode (the 32 KB flash cache modelled), 1500 ms each except the stack
case at 3000 ms. `8f8d7d4` is `1e2999a` plus a test-only fix, so its firmware is byte-identical.

| case | db66c5b base | 1e2999a | 8f8d7d4 |
|---|---:|---:|---:|
| busy_phys misses/half | 275.5 | 184.7 | 184.7 |
| psy_phys misses/half | 692.7 | 524.0 | 524.0 |
| psy_stack misses/half | 774.3 | 694.0 | 694.0 |
| busy_phys worst half us | 2680 | 2399 | 2399 |
| psy_phys worst half us | 2997 | 2986 | 2986 |
| psy_stack worst half us | 2804 | 2599 | 2599 |
| busy_phys CPU % | 68.0 | 66.0 | 66.0 |
| psy_phys CPU % | 74.6 | 72.3 | 72.3 |
| psy_stack CPU % | 78.9 | 76.2 | 76.2 |
| psy_phys late halves | 4 | 2 | 2 |
| psy_stack voices shed | 15 | 3 | 3 |

A build that moves a hot function between flash and RAM shifts every later address, so compare one
worktree against another only when both are built from that same tree (`docs/fm1-emu-test.md`).
