# Agent notes (Araminta)

Project rules for coding agents. Global workflow still lives in `~/AGENT.md`.

## Host tests

`tests/run_tests.sh` is the full gate. It takes several minutes. It does **not** filter on argv: `tests/run_tests.sh sound15` still runs everything.

### Iterate

1. After `./build.sh` once (needs `build/gen/`).
2. Run only the target that covers the change: `tests/run_one.sh <target>`.
3. Done when that target exits 0 on the behaviour you touched.

`tests/run_one.sh` is the edit loop, not the final gate. It compiles and runs
one binary (or the web pair) from the same sources as `tests/run_tests.sh`, at
`-O0` where that is faster (`ui_render` keeps `-O1`):

```
tests/run_one.sh list            # target list
tests/run_one.sh motion          # motion / ARP / song
tests/run_one.sh ui ui_render    # UI stubs plus draw / layout lint
tests/run_one.sh editor          # writes build/host/menu.json
tests/run_one.sh project
tests/run_one.sh param           # tests/param_contract_test.c
tests/run_one.sh web             # desc.json + menu.json, then node web/test_web.mjs
```

Targets: `motion`, `ui`, `ui_render`, `editor`, `project`, `param`, `web`,
`all` (execs `tests/run_tests.sh`), and `list`.

If a test has no target, compile and run it by hand, matching the `$CC` line in
`tests/run_tests.sh` for that test.

Web-only edits: `tests/run_one.sh web` (or `node web/test_web.mjs`; add
`web/test_backup.mjs` if backup paths moved).

If you must invoke the full script while iterating, use `SANITIZE=0 tests/run_tests.sh` so ASan/fuzz are skipped. Emulator tests already skip when `emcc` is missing.

The final gate is unchanged: `SANITIZE=1 tests/run_tests.sh` before the final
push, and the full gate instead of the targeted targets when 3+ binaries are
touched (see Handoff below).

### Handoff

Gate by blast radius of the edit, not the full script by default.

1. List the distinct `build/host/*` binaries that cover the touched paths (use the map below; `ui_test` and `ui_render` count as two).
2. If that list has **1 or 2** binaries: compile and run each. If `web/` also moved, run the matching `node web/test_*.mjs` too. Docs-only / no behaviour change: no suite.
3. If the list has **3 or more** binaries: run full `tests/run_tests.sh` with sanitizers on (default).
4. Done when every required command exits 0 (full suite: `ALL HOST TESTS PASSED`).

Firmware + web in one change: run the host side from steps 2–3 **and** the web tests; do not skip one because the other passed.

Full suite on request, or before a commit/PR the user asks for, still wins over this rule when they ask for it.

### Before the final push

Run the full gate **once, with sanitizers on**:

```
SANITIZE=1 tests/run_tests.sh
```

`SANITIZE=1` is the gate. Do not also run the unsanitized suite for the same change, and do not push
until it prints `ALL HOST TESTS PASSED`.

## Rough map (source → host binary)

| Touch | Binary under `build/host/` |
| --- | --- |
| `firmware/src/ui*.c`, help, menus | `ui_test` (also `ui_render` for draw/layout) |
| DRUMS engine (`eng_drums.c`, `drums_voice.c`) | `drums_test` (also `regress` for presets/CPU) |
| ANALOG filter / ENV SYNC / sound 1.5 | `sound15_test` |
| insert chain / FX order | `inschain_test` |
| motion / ARP / song | `motion_test` |
| locks / special ids / PITCH / FX motion | `param_contract_test` |
| projects / FUN migration | `project_test` |
| editor protocol / MENU | `editor_test` |
| golden / CPU / voice budgets | `regress` |
