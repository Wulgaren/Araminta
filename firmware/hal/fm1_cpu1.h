/* SPDX-License-Identifier: GPL-3.0-only */
/* The AC79's second core. From X0X 1.0.5 (github.com/charlesvestal/fm1-x0x, GPL-3.0: hal/fm1_cpu1.h), with
 * what Melodee's dual-core audio (github.com/keremimo/melodee, GPL-3.0) found on the device and Jangada's
 * (github.com/zednaked/jangada, GPL-3.0) measurements and hardening. Ported with Felucca 1.6.1's second-core
 * work into Araminta. No supply raise (BOOST) here.
 *
 * Araminta: the second core is always built in on the target (no MENU toggle, no single-core build).
 * Failure policy differs from Felucca's "one bad job and it is off until re-enabled" (1a / 2b):
 *   - a job that fails (wrong job read, or no answer in 4 ms) holds core 1 and the caller renders that
 *     block's parts itself (a misread costs nothing at all, core 1 never touched the sums; an overrun
 *     costs one glitch);
 *   - the main loop (main.c) starts core 1 again, up to FM1_C1_RESTARTS times within a clean-run
 *     window: a hiccup comes back by itself;
 *   - a core that keeps failing the window, or never reports in at start, is done for the session
 *     (fm1_c1_dead): one core, silently (the console still shows the counts).
 *
 * Start (the AC79 SDK's EnableOtherCpu): core 1's interrupt bank off (0x1EEF300..), its entry
 * address into the word at 0x01C7FFF8, bit 3 of 0x10008 held while it starts, C1_CON (0x1EEE004)
 * bit 3 set (enabled) and bit 1 cleared (out of hold). It boots through the chip's ROM, which the
 * PC limits refuse, so they are open until it has reported in from fm1_c1_main. Hold is bit 1
 * set, then bit 3 cleared.
 *
 * Work: one job at a time, a function and an argument; core 0 hands it over (mb.job) and waits
 * for it (mb.done), single writer each way, csync around each (the cores share the caches).
 *
 * Units that cannot run it (fm1-x0x#10, measured by Jangada, github.com/zednaked/jangada): on some
 * FM-1s core 1 reads words that are not in RAM (0x00200000 where core 0 reads 0), idle now and then,
 * often under load. So core 1 runs only the next job, and only when its check word (the function,
 * the argument and the job's number, written before the job) matches; the jobs it finished stay in a
 * register (Melodee's, keremimo/melodee#21: a stale function read as the next job fails the check).
 * Anything else it counts (mb.bad) and leaves: the job bookkeeping is then one behind and cannot be
 * resumed (core 1's own copy of "done" is a register), so core 0 holds it and the main loop starts
 * it again. The mailbox keeps each core's writes on cache lines of their own (Jangada's layout).
 *
 * Flash: the waiting loop is in RAM (.c1_text), so an idle core 1 never fetches from the flash;
 * it works only for the audio interrupt, which waits for it before it returns, and core 0 turns
 * the flash off only with interrupts off. So the flash is never off under it. */
#pragma once
#include <stdint.h>
#include "fm1_sys.h"                             /* FM1_C1_CON, fm1_cpu1_halt (FM1_CPU1_USED) */
#if !FM1_CPU1_USED
#error "fm1_cpu1.h: only with the second core built in (FM1_CPU1_USED=1, the target build)"
#endif
#define FM1_C1_CLK (*(volatile uint32_t *)0x00010008u)
#define FM1_C1_T4 (*(volatile uint32_t *)0x10804u)       /* TIMER4, 24 MHz */
#define FM1_C1_SYNC() __asm__ volatile("csync" ::: "memory")

extern void fm1_c1_entry(void);
extern uint32_t _c1_ustack[], _c1_sstack_top[];
#define FM1_C1_STACK_WORDS (4096u / 4u)          /* fm1_cpu1.S; Jangada measured ~1.6 KB at a part's deepest (PHYS SYMP) */
#define FM1_C1_MARK 0x43314D4Bu                  /* "C1MK": how deep core 1's stack has gone */
typedef struct {
    volatile uint32_t job, arg;                 /* core 0: job + 1 hands fn(arg) over */
    void (*volatile fn)(uint32_t);
    volatile uint32_t check;                    /* FM1_C1_CHECK of fn, arg and job */
    uint32_t pad0[12];
    volatile uint32_t alive, done;              /* core 1: started; the last job it finished */
    volatile uint32_t bad, bad_job, bad_done, bad_fn;   /* jobs it would not run: how many, the last one */
    uint32_t pad1[10];
} fm1_c1_mb_t;
static fm1_c1_mb_t fm1_c1_mb __attribute__((aligned(64)));
volatile uint32_t fm1_c1_trace;                 /* breadcrumbs (fm1_cpu1.S, fm1_c1_main) */
static uint8_t fm1_c1_on;                       /* started and answering: core 0 may hand it work */
#define FM1_C1_CHECK(fn, arg, job) ((uint32_t)(uintptr_t)(fn) ^ (arg) ^ (job) * 0x9E3779B1u)
static uint8_t fm1_c1_gave_up;                  /* the last failure: 1 = it read a wrong job, 2 = it did not finish one */
static uint8_t fm1_c1_dead;                     /* done for this session (start failed, or fm1_cpu1_RESTARTS in the window) */
static uint8_t fm1_c1_restarts;                 /* restarts since the window last cleared (main.c: fm1_cpu1_service) */
static uint16_t fm1_c1_ok;                      /* jobs finished since the last failure: clears the restarts */
#define FM1_C1_RESTARTS 3u                      /* a hiccup comes back; this many within the window and it is dead */
#define FM1_C1_OK_WINDOW 4096u                  /* clean jobs (~3 s of blocks) and the restart budget is whole again */
#define FM1_C1_BAIL 24000u                      /* 1 ms in TIMER4 ticks: a healthy job is done well within (a half
                                                 * buffer's budget is 2.9 ms). Waiting past it and redoing the work
                                                 * would blow the deadline and click; giving up at it, mix_block still
                                                 * finishes the block in time (fx.c c1_done) */
static uint32_t fm1_c1_wait_max;                /* the longest wait for a job, TIMER4 ticks (24 / us) */
static uint8_t fm1_c1_marked;                   /* its stack was marked (fm1_cpu1_start): fm1_cpu1_stack_used 0 before */

void fm1_c1_main(void);
void __attribute__((section(".c1_text"), noreturn, used)) fm1_c1_main(void)
{
    /* its own guards, as Melodee sets them (its fm1_core1_main): a stack limit in core 1's EMU bank
     * (core 0's is at 0x1EEF0D0) over both its stacks less the lowest 256 bytes, and EMU_CON bit 2
     * and bits 16..20 off, bit 3 (the stack limit) on */
    uint32_t lo = (uint32_t)(uintptr_t)_c1_ustack + 256u, hi = (uint32_t)(uintptr_t)_c1_sstack_top - 1u;
    uint32_t done, last_bad = 0xFFFFFFFFu;
    *(volatile uint32_t *)0x1EEF2D8u = hi;
    *(volatile uint32_t *)0x1EEF2DCu = lo;
    *(volatile uint32_t *)0x1EEF2E0u = hi;
    *(volatile uint32_t *)0x1EEF2E4u = lo;
    *(volatile uint32_t *)0x1EEF2D0u = (*(volatile uint32_t *)0x1EEF2D0u & ~((1u << 2) | (0x1Fu << 16))) | (1u << 3);
    fm1_c1_trace = 0xC1000002;
    done = fm1_c1_mb.done;
    fm1_c1_mb.alive = 1;
    FM1_C1_SYNC();
    for (;;) {
        uint32_t j = fm1_c1_mb.job;
        FM1_C1_SYNC();
        if (j == done)
            continue;
        if (j == done + 1u) {
            void (*fn)(uint32_t) = fm1_c1_mb.fn;
            uint32_t arg = fm1_c1_mb.arg;
            if (fn && fm1_c1_mb.check == FM1_C1_CHECK(fn, arg, j)) {
                fn(arg);                        /* the work itself runs from the flash */
                done = j;
                FM1_C1_SYNC();
                fm1_c1_mb.done = j;
                FM1_C1_SYNC();
                continue;
            }
            fm1_c1_mb.bad_fn = (uint32_t)(uintptr_t)fn;
        }
        if (j != last_bad) {                    /* each wrong value once (it stays until core 0 holds it) */
            last_bad = j;
            fm1_c1_mb.bad_job = j;
            fm1_c1_mb.bad_done = done;
            FM1_C1_SYNC();
            fm1_c1_mb.bad++;
            FM1_C1_SYNC();
        }
    }
}

/* hold core 1: fm1_cpu1_halt (fm1_sys.h, registers only; every reset path calls it, and fm1_cstart first thing at
 * boot, before the RAM it may still be reading is cleared, after a reset that did not hold it) */
static void fm1_cpu1_hold(void)
{
    fm1_cpu1_halt();
    fm1_c1_on = 0;
}

/* 0 = core 1 runs and answered; else it is held again. The top of RAM must be writable
 * (fm1_guard_unlock_top) and the PC limits open (fm1_guard_pc_open): the caller's. Works held or not
 * (the main loop's restart, fm1_cpu1_service): it resets the job bookkeeping a job needs to resume on */
static int fm1_cpu1_start(void)
{
    uint32_t saved, t0, i;
    if (fm1_c1_on)
        return 0;
    fm1_cpu1_hold();
    for (i = 0; i < FM1_C1_STACK_WORDS; i++)
        _c1_ustack[i] = FM1_C1_MARK;
    fm1_c1_marked = 1;
    fm1_c1_mb.alive = 0;
    fm1_c1_trace = 0;
    fm1_c1_mb.done = fm1_c1_mb.job;
    fm1_c1_mb.fn = 0;
    FM1_C1_SYNC();
    for (i = 0; i < 32u; i++)                   /* its interrupt bank: all off (core 0's is at 0x1EEF100) */
        *(volatile uint32_t *)(0x1EEF300u + 4u * i) = 0;
    *(volatile uint32_t *)0x01C7FFF8u = (uint32_t)(uintptr_t)&fm1_c1_entry;
    saved = FM1_C1_CLK;
    FM1_C1_CLK = saved | 0x8u;
    FM1_C1_SYNC();
    FM1_C1_CON |= 0x8u;
    FM1_C1_CON &= ~0x2u;
    t0 = FM1_C1_T4;
    while (!fm1_c1_mb.alive && FM1_C1_T4 - t0 < 24000u * 20u)   /* 20 ms */
        FM1_C1_SYNC();
    FM1_C1_CLK = saved;
    if (!fm1_c1_mb.alive) {
        fm1_cpu1_hold();
        return -1;
    }
    fm1_c1_on = 1;
    return 0;
}

/* hand core 1 a job: 1 = it took it (fm1_cpu1_wait before touching what it works on) */
static inline int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg)
{
    uint32_t job;
    if (!fm1_c1_on)
        return 0;
    job = fm1_c1_mb.job + 1u;
    fm1_c1_mb.fn = fn;
    fm1_c1_mb.arg = arg;
    fm1_c1_mb.check = FM1_C1_CHECK(fn, arg, job);
    FM1_C1_SYNC();
    fm1_c1_mb.job = job;
    FM1_C1_SYNC();
    return 1;
}

/* 0 = done; -1 = it read a wrong job or did not finish in time. Core 1 is held either way (its job
 * bookkeeping is one behind and cannot be resumed) and the rest of the block's work is the caller's again:
 * a misread costs nothing (core 1 left the sums alone), a stall costs at most the part it was cut in (its
 * state advances twice for that one block). The main loop starts core 1 again (fm1_cpu1_service) unless
 * it is dead */
static inline int fm1_cpu1_wait(void)
{
    uint32_t t0 = FM1_C1_T4;
    for (;;) {
        FM1_C1_SYNC();
        if (fm1_c1_mb.bad) {                    /* a unit that cannot run it */
            fm1_cpu1_hold();
            fm1_c1_gave_up = 1;
            return -1;
        }
        if (fm1_c1_mb.done == fm1_c1_mb.job) {
            if (FM1_C1_T4 - t0 > fm1_c1_wait_max)
                fm1_c1_wait_max = FM1_C1_T4 - t0;
            if (fm1_c1_ok < FM1_C1_OK_WINDOW) {
                fm1_c1_ok++;
            } else {                             /* a clean run long enough: a past hiccup does not count any more */
                fm1_c1_ok = 0;
                fm1_c1_restarts = 0;
            }
            return 0;
        }
        if (FM1_C1_T4 - t0 > FM1_C1_BAIL) {     /* (it faulted or stalled: held, mix_block covers the block) */
            fm1_cpu1_hold();
            fm1_c1_gave_up = 2;
            return -1;
        }
    }
}

/* the main loop's, once an iteration: after a failure core 1 is held and its block was core 0's;
 * start it again (a hiccup comes back) or, out of restarts in the clean-run window, for good */
static void fm1_cpu1_service(void)
{
    if (fm1_c1_on || fm1_c1_dead || !fm1_c1_gave_up)
        return;
    if (fm1_c1_restarts >= FM1_C1_RESTARTS) {
        fm1_c1_dead = 1;
        return;
    }
    fm1_c1_restarts++;
    fm1_c1_ok = 0;
    fm1_c1_gave_up = 0;
    if (fm1_cpu1_start() != 0)                  /* it never reported in: a dead core, not a hiccup */
        fm1_c1_dead = 1;
}

/* bytes of core 1's stack ever used (the mark left untouched below them) */
static uint32_t fm1_cpu1_stack_used(void)
{
    uint32_t i = 0;
    if (!fm1_c1_marked)                         /* never started this session: none, not the whole unmarked stack */
        return 0;
    while (i < FM1_C1_STACK_WORDS && _c1_ustack[i] == FM1_C1_MARK)
        i++;
    return (FM1_C1_STACK_WORDS - i) * 4u;
}
