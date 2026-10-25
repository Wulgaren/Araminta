# Agent notes (Araminta)

Project rules for coding agents. Global workflow still lives in `~/AGENT.md`.

## Host tests

`tests/run_tests.sh` is the full gate. It takes several minutes. It does **not** filter on argv: `tests/run_tests.sh sound15` still runs everything.

### Iterate

1. After `./build.sh` once (needs `build/gen/`).
2. Compile and run only the host binary that covers the change. Match the `$CC` line in `tests/run_tests.sh` for that test.
3. Done when that binary exits 0 on the behaviour you touched.

Example (UI stubs):

```
mkdir -p build/host
cc -w -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm
./build/host/ui_test
```

Web-only edits: `node web/test_web.mjs` (and `web/test_backup.mjs` if backup paths moved).

If you must invoke the script while iterating, use `SANITIZE=0 tests/run_tests.sh` so ASan/fuzz are skipped. Emulator tests already skip when `emcc` is missing.

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
| ANALOG filter / ENV SYNC / sound 1.5 | `sound15_test` |
| insert chain / FX order | `inschain_test` |
| motion / ARP / song | `motion_test` |
| projects / FUN migration | `project_test` |
| editor protocol / MENU | `editor_test` |
| golden / CPU / voice budgets | `regress` |
