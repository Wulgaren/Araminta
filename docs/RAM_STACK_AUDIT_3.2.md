# RAM and stack audit (v3.2)

Task 7 of the v3.2 plan: find the real CPU0/CPU1 stack use and check Melodee 1.5's RAM wins against
Araminta. This branch adds instrumentation only: no stack was moved, resized or repainted after boot.

No FM-1 was attached while this was written. Every measured number is PENDING; the report is good
enough to plan the follow-up linker PR, not to size the stacks from alone.

**Number labels**

| Label | Meaning |
|-------|---------|
| VERIFIED | read from the source, the linker script, the ELF symbol table or the build output |
| STATIC | estimate from `build/felucca.dis` (prologue frames + call graph, no margin) |
| PENDING | needs the hardware workload below |

## Reservations now (VERIFIED)

| Stack | Range | Size | Source | Guard |
|-------|-------|------|--------|-------|
| CPU0 user (main loop) | `0x01C74100`-`0x01C7A000` | 24,320 B | `firmware/app.ld:34` | 256 B below (`_guard0` `0x01C74000`) |
| CPU0 supervisor (IRQ/exception) | `0x01C7A100`-`0x01C7C000` | 7,936 B | `firmware/app.ld:35` | 256 B below (`_guard1` `0x01C7A000`) |
| CPU1 user (render jobs) | `0x01C08240`-`0x01C09240` | 4,096 B | `firmware/hal/fm1_cpu1.S:26-27` | 256 B at the bottom of its window (`fm1_cpu1.h:85`) |
| CPU1 supervisor | `0x01C09240`-`0x01C09340` | 256 B | `firmware/hal/fm1_cpu1.S:29-32` | none (no interrupts on core 1) |

- POOL region: `0x01C1A000` + `0x5A000` = ends at `0x01C74000` (`firmware/app.ld:19`), directly under
  `_guard0`. POOL *content* ends at `0x01C70CE0`: 355,552 B used of 368,640, **13,088 B spare**
  (VERIFIED, `./build.sh`: `pool 355552 B of 368640`).
- RAM `.data+.bss`: 69,360 B of 73,728, 4,368 B headroom (VERIFIED, same build line). The CPU1 stacks
  live in `.bss` (`_c1_ustack`, `_c1_sstack`, `_c1_sstack_top` from `objdump -t`), so freeing CPU1
  stack bytes grows RAM headroom, not POOL, unless the RAM/POOL boundary in `app.ld` moves.
- EMU stack limit is one window over both CPU0 stacks (`fm1_guard.h:38,67-77`); CPU1 sets its own over
  both its stacks, less the low 256 B (`fm1_cpu1.h:85-91`). The write guards cover both 256 B bands
  (`fm1_guard.h:74-78`).
- `FM1_C1_STACK_WORDS` is 4096 B with the comment "Jangada measured ~1.6 KB" (`fm1_cpu1.h:50`).

## Instrumentation in this branch

- `firmware/src/main.c:313-338`: `FM1_STACK_MARK` (`"S0MK"`), `fm1_stack_paint`, and the readers
  `fm1_stack_used_user()` / `fm1_stack_used_super()`.
- `fm1_cstart` (`main.c:376`) paints after the section copies and before `fm1_guard_enable`, with
  interrupts off. The user stack is painted from `_ustack_lo` up to `(sp - 256) & ~3` (the boot frame
  and any spill slot under `sp` are left alone); the whole supervisor stack is painted, since nothing
  has used it before the first interrupt.
- A word still at the mark was never written since boot. `used` = stack top minus the lowest word that
  is no longer the mark. It therefore counts the boot frame and the reader's own frame: conservative,
  never optimistic. As with `fm1_cpu1_stack_used`, a live word equal to the mark reads as unused.
- `firmware/hal/fm1_sys.h:36`: `fm1_sp()` (the build's `mmio_check` forbids inline asm under `src/`).
- `firmware/src/console.c:220-224`: one line in `status`:
  `c0_stack_used user/super <bytes> <bytes>`. CPU1 keeps its line in `c1` (`c1_stack_used`).
- `web/emu/felucca_web.c` stubs the four stack symbols and `fm1_sp` (the browser has no device
  stacks and never calls `fm1_cstart`); the readers stay at 0 there.
- Nothing else changed: same addresses, same sizes, same guards.

## Method

1. **Runtime, painting (PENDING).** Boot once, then read `status` (CPU0) and `c1` (CPU1) after each
   item in the workload. The readers are monotonic per boot, so one reading per stage is enough.
   Minimum free = reservation - `used` (CPU0 user / supervisor, CPU1 user). Record where it stopped
   growing.
2. **Static (STATIC).** JieLi clang 4.0.1 rejects both flags (`VERIFIED`):
   `clang: error: unknown argument: '-fstack-usage'` and the same for `-fcallgraph-info=su`. So the
   figures come from `build/felucca.dis` (`llvm-objdump -d`, written by `tools/build.py`): per function,
   the maximum `sp += -N` / `[--sp] = {…}` offset, then the longest call path (4 B return address per
   `call`, 0 for a tail `goto`). The cost includes callee frames. Indirect `call rN` cannot be
   followed; the engine vtable bound is added separately below. Frames are not rounded up, no 25 %
   margin is included.
3. **Compare (rule from the plan).** If measured and static disagree by more than 2x, do not shrink.
   New size = max(static worst path, measured high-water) + at least 25 %.

Reproduce the static side:

```sh
./build.sh                                  # writes build/felucca.dis and the RAM/pool line
# per function: largest sp offset; then follow callees from fm1_main / isr_alnk0 / c1_parts
```

## Hardware workload checklist (PENDING)

Read `status` and `c1` after each step (or at the end of a step and again after the next); fill the
minimum free per stack.

| # | Workload | CPU0 user | CPU0 super | CPU1 user |
|---|----------|-----------|------------|-----------|
| 1 | Boot to the UI, no keys | | | |
| 2 | Project load and save (storage paths are deep) | | | |
| 3 | Every engine at full poly, FM6 and PHYS SYMP included | | | |
| 4 | All FX + GLITCH latched | | | |
| 5 | Song chain playing | | | |
| 6 | MIDI in and out at full rate | | | |
| 7 | OTA / USB menu open | | | |
| 8 | Web editor connected (editor SysEx) | | | |
| 9 | 30 minutes of the stress test | | | |

Also read `c1_stack_used` and both values of the `c0_stack_used` line: those are the high-water marks
after the whole run. If the console is not attached, the numbers exist only there; the TECH page
does not show them in this branch.

## Static findings (STATIC)

Largest frames in the image (bytes, `build/felucca.dis`):

| Frame | Function | Note |
|-------|----------|------|
| 2,656 | `ed_handle` | editor SysEx dispatcher, the deepest single frame |
| 1,192 | `fm1_main` | the main loop holds its whole work set |
| 1,096 | `up_load` | user-preset load |
| 1,056 | `ui_input` | menu callbacks called indirectly from here |
| 1,040 | `draw_graph` | graph page |
| 992 | `undo_swap` | project undo |
| 976 | `phys_render` | PHYS (deepest engine render; PHYS SYMP inside) |
| 928 | `notes_shift` | sequencer edit |
| 708 | `proj_unpack` | project load buffer |
| 592 | `settings_poll` | settings save |
| 568 | `up_values` | preset values |
| 540 | `trio_render` | TRIO engine |
| 496 | `fm6_render` | FM6 engine |
| 424 | `formant_render` | FORMANT engine |
| 352 | `rev_cloud` | HALL/CLOUD reverb |

Worst paths:

- **CPU0 user, 5,652 B** (adds the 4 B return address of crt0's `call fm1_cstart`):
  `_start` -> `fm1_cstart` (4) -> `fm1_main` (1,192) -> `ed_service` (60) -> `ed_handle` (2,656)
  -> `up_load` (1,096) -> `up_values` (568) -> `ins_migrate` (48), plus 4 B for each call.
  The editor path wins because the main loop is in it. The UI path is lower: `ui_input` (1,056) plus
  its callbacks is 2,840 B as a root. `draw_graph` and `undo_swap` are reached from there with less
  total depth.
- **CPU0 supervisor, 1,868 B** with TIMER5 nested in the audio render:
  `isr_alnk0` (28, the wrapper's 7 saved words) -> `fm1_alnk0_irq` (128) -> `mix_part` (100)
  -> `track_render_audio` (168) -> engine vtable -> `phys_render` (976 frame, 1,276 with its
  callees) = 1,716 B; nested TIMER5 adds `isr_timer5` (152) -> 1,868 B.
  Without the engine vtable (inserts/reverb only) the audio path is 1,020 B.
- **CPU1 user, 1,608 B**: `c1_parts` (52) -> `mix_part` (100) -> `track_render_audio` (168)
  -> engine vtable -> `phys_render` (1,276). This is 39 % of 4,096 and sits next to Jangada's ~1.6 KB
  on the same SDK lineage: a useful cross-check, not a measurement of this unit.
- **CPU1 supervisor, ~140 B**: fault frame (9 + 16 saved words = 100 B), call, `fm1_fault_c` (36).
  Core 1 has no interrupts, so this is the whole use; it fits the 256 B reservation with 116 B spare.
- **CPU0 exception path, 440 B**: the fatal frame plus `fm1_fault_c` (336). It can nest into the audio
  path; the sum is still far below 7,936 B.

Caveats for the STATIC column: function pointers are not followed (engine vtables at
`voice.c:729`, menu callbacks at `ui_input` depth 1,056 and `ui_draw_page` depth 388, main-loop
callbacks at `fm1_main` depth 1,192). The engine bound is added above using `phys_render`; a callback
that itself re-enters `fm1_main` would invalidate it, which none does today. Recursion: none found in
the call graph. Tail calls count without the 4 B return address. Volatile mark collisions are
possible in theory.

## Where the bytes would go

| Stack | Reserved | Static worst | Measured min free | Proposed | Freed |
|-------|----------|--------------|-------------------|----------|-------|
| CPU0 user | 24,320 | 5,652 | PENDING | 12,288 | 12,032 |
| CPU0 supervisor | 7,936 | 1,868 | PENDING | 6,144 | 1,792 |
| CPU1 user | 4,096 | 1,608 | PENDING | 4,096 (3,072 if measured supports it) | 0 (1,024) |
| CPU1 supervisor | 256 | 140 | PENDING | 256 | 0 |

If the measured numbers land near the static ones, the plan's sizes hold: 12,288 B user is 2.2x the
static worst, 6,144 B is 3.3x, and 3,072 B for core 1 is 1.9x. Freed: 13,824 B for CPU0 (VERIFIED
arithmetic), plus 1,024 B of `.bss` on core 1. The CPU0 bytes go to POOL by moving `_guard0`,
`_ustack_lo`, `_ustack_top`, `_guard1` and `_sstack_lo` up and growing `POOL LENGTH`; `_sstack_top`
stays at `0x01C7C000` so the NOINIT boundary does not move. The CPU1 bytes only reach POOL if the
RAM/POOL boundary also moves 1 KB.

## Melodee 1.5's other wins, checked

| Win | Melodee | Araminta | Verdict |
|-----|---------|----------|---------|
| Shared stereo FX filter coefficients | -1,200 B | The SVF coefficients are already shared L/R: one `pf.lk[3]` / `pf.hk[3]` pair feeds both channels (`perform.c:160,374-375,802-813`). Insert FX store state per track/stage, not coefficients (`fx.c:1205-1209`); `ins_lines` is delay history (`fx.c:1222`), not a coefficient copy. Master clip gain is scalar (`fx.c:1771`); reverb/delay are mono sends with scalar size/damp (`fx.c:682-1021`). 0 B per instance, 0 B total. | N/A: nothing to de-duplicate |
| Quarter-wave FM6 log table | -2,560 B | Every FM6 table is `const` and lands in XIP `.text`: `objdump -t` shows `FM6_EXP2` 0x1004 B, `SINE` 0x802 B, `FM6_ALG`, `FM6_DETUNE`, `FM6_FINE`, and the rest all in `.text` at 0x02xxxxxx. RAM `.data` is 536 B of variables and `.c1_text` is 160 B of code; no FM6 table is copied. RAM cost: 0 B. | N/A |
| Halved PUNCH history | -32 KiB while armed | PUNCH is a per-hit gain ramp with no history (`eng_drum.c:76-90,291`). The buffer effects already borrow the slicer's `sl_buf` (`perform.c:36,64,472`), one stereo loop over the four 8 KB track recordings (`slicer.c:52`: 4 x 4,096 x 2 B = 32,768 B). Halving would cut the loop from 371 ms to 185 ms and the per-track recording from 186 ms to 93 ms, under the plan's low-BPM limit. | N/A (already shared); do not halve |
| Compressed resident code | — | Code runs from XIP: `.text` 541,032 B at 0x02000120; only `.ram_text` 2,924 B (flash driver) and `.c1_text` 160 B (core 1 idle loop) are copied to RAM (VERIFIED, `objdump -h` and `./build.sh`). | N/A |

## Recommendation

1. **Do not resize any stack in this PR.** It is instrumentation plus this report. The static margins
   are comfortable, but no hardware reading exists yet and the plan's own rule says not to shrink when
   static and measured differ by more than 2x.
2. Run the workload checklist on the unit, then fill the PENDING columns. The expected numbers are
   CPU0 user <= 8 KB, CPU0 supervisor <= 2.5 KB, CPU1 user <= 2 KB. If they land there, the follow-up
   PR is: CPU0 user 12,288 B, supervisor 6,144 B, CPU1 user 3,072 B, which returns 14,848 B in total
   (13,824 B to POOL, 1,024 B to RAM headroom, or to POOL if the RAM boundary moves).
3. If a reading reaches the static worst but with a smaller margin (for example CPU0 user > 8 KB because
   a deeper editor/menu path was missed), keep 24,320 B and report the path instead of shrinking.
4. Melodee's other three wins do not apply: the coefficient copies and the FM6 table are not in RAM in
   Araminta, and the slicer buffer is already shared. The only remaining lever is the stack sizes above,
   plus the existing pool headroom.

## PENDING

- Measured high-water for all four stacks under the checklist.
- Whether the 1,868 B audio worst case is what the device reaches (engine vtable, PHYS SYMP, nested
  TIMER5).
- The largest indirect UI/editor callback path, which the static estimate follows only up to the
  direct-call graph.
- Measured/static ratio, and therefore whether the 12/6/3 KB proposal stands.
