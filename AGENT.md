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

Before handing control back after firmware/UI/host-test edits: run a full `tests/run_tests.sh` with sanitizers left on (default). Done when it prints `ALL HOST TESTS PASSED`.

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
