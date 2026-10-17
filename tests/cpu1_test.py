# SPDX-License-Identifier: GPL-3.0-only
"""Exercise core-1 recovery with real HAL code and simulated AC79 registers."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def host_registers(source: str) -> str:
    """Redirect AC79 register accesses to the simulated register bank."""
    return re.sub(
        r"\*\(volatile uint32_t \*\)(\([^\n;]+?\)|0x[0-9A-Fa-f]+u)",
        r"*test_register(\1)",
        source,
    )


def main() -> None:
    hal = (ROOT / "firmware/hal/fm1_cpu1.h").read_text(encoding="utf-8")
    hal = hal.replace('#include "fm1_sys.h"', '// simulated system registers')
    hal = hal.replace('__asm__ volatile("csync" ::: "memory")', "test_sync()")
    guard = (ROOT / "firmware/hal/fm1_guard.h").read_text(encoding="utf-8")
    guard = guard.replace('#include "fm1_irq.h"', '// simulated system registers')
    source = (ROOT / "firmware/src/main.c").read_text(encoding="utf-8")
    start = source.index("        if (!fm1_c1_on && !fm1_c1_dead && fm1_c1_gave_up)")
    end = source.index("\n        {", start)
    restart = source[start:end]
    harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define FM1_CPU1_USED 1
static uint32_t registers[0x400 / 4], clock_reg, timer_reg, con_reg, entry_reg;
static unsigned protected_writes;
static int boot_answers = 1;
static volatile uint32_t *test_register(uint32_t address) {
    if (address == 0x01C7FFF8u) {
        if (registers[0x348 / 4] & 4u) protected_writes++;
        return &entry_reg;
    }
    if (address == 0x10008u) return &clock_reg;
    if (address == 0x10804u) return &timer_reg;
    if (address >= 0x1EEE000u && address < 0x1EEE400u)
        return &registers[(address - 0x1EEE000u) / 4];
    /* Core-1 interrupt and EMU registers are outside the CPU0 guard bank. */
    static uint32_t core1_registers[0x200 / 4];
    if (address >= 0x1EEF200u && address < 0x1EEF400u)
        return &core1_registers[(address - 0x1EEF200u) / 4];
    fprintf(stderr, "unexpected register %x\n", address);
    exit(2);
}
#define FM1_C1_CON con_reg
#define FM1_DBG_WR_EN registers[0]
#define FM1_DBG_MSG_CLR registers[1]
#define FM1_DBG_EN registers[2]
#define FM1_EMU_CON registers[3]
uint32_t _c1_ustack[1024], _c1_sstack_top[1], _rt_start[1], _rt_end[1], _c1_end[1];
char _guard0[1], _ustack_lo[1], _guard1[1], _sstack_lo[1], _sstack_top[1], _etext[1];
void fm1_c1_entry(void) {}
static void fm1_cpu1_halt(void) { con_reg = 2; }
static void test_sync(void);
'''
    checks = r'''
static void test_sync(void) {
    timer_reg += 24000u;
    if (boot_answers && (con_reg & 0xau) == 0x8u)
        fm1_c1_mb.alive = 1;
}
static void restart_from_main(void) {
RESTART
}
#define CHECK(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "%s\n", message); return 1; } } while (0)
static uint32_t job_result;
static void work(uint32_t value) { job_result = value; }
int main(void) {
    fm1_guard_lock_top();
    fm1_c1_gave_up = 1;
    restart_from_main();
    CHECK(protected_writes == 0, "restart wrote the boot entry into protected RAM");
    CHECK(fm1_c1_on && !fm1_c1_dead, "healthy core did not restart");
    CHECK(FM1_C0_WR_LIMIT_EN & 4u, "restart left top RAM writable");
    CHECK(FM1_PC_LIMIT0_L == 0x02000120u, "restart left PC limits open");
    puts("core-1 protected restart: ok");

    CHECK(fm1_cpu1_run(work, 17), "core refused initial job");
    fm1_c1_mb.bad++;                     /* core 1 rejected the mailbox read */
    CHECK(fm1_cpu1_wait() == -1 && !fm1_c1_on, "rejected job did not hold core 1");
    restart_from_main();
    CHECK(fm1_c1_mb.bad == 1, "restart lost cumulative mailbox diagnostics");
    CHECK(fm1_cpu1_run(work, 42), "restarted core refused the next job");
    fm1_c1_mb.fn(fm1_c1_mb.arg);           /* core 1 completed the next job */
    fm1_c1_mb.done = fm1_c1_mb.job;
    CHECK(fm1_cpu1_wait() == 0 && job_result == 42,
          "completed job after recovery was rejected by an earlier mailbox error");
    CHECK(fm1_c1_on && !fm1_c1_dead, "completed job disabled recovered core");
    puts("core-1 recovery after mailbox error: ok");
    return 0;
}
'''.replace("RESTART", restart)
    with tempfile.TemporaryDirectory(prefix="araminta-cpu1-") as directory:
        build = Path(directory)
        test_source = build / "cpu1_test.c"
        executable = build / "cpu1_test.exe"
        test_source.write_text(harness + host_registers(guard) + host_registers(hal) + checks, encoding="utf-8")
        subprocess.run(["gcc", "-std=c99", "-Wno-int-to-pointer-cast", str(test_source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
