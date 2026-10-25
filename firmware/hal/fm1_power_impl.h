/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* fm1_power.h's boot-only helpers that need inline asm (hal/ is the only
 * place the build's check allows it). Included by firmware/src/power.c, so the
 * code lands with the rest of the supply setup, after the audio and UI sources. */
#pragma once
#include "fm1_power.h"

#ifndef FM1_POWER_HOST_TEST
/* Stock delay(1000), not 1000 us: 1000 iterations of the same increment / test.
 * A volatile count keeps the loop (the memory clobber also keeps each delay
 * between its P33 operations). A C loop, not the stock's inline asm goto: the
 * asm changes the whole unity build's block layout and the audio ISR's
 * target_budget.py spans with it. */
static __attribute__((noinline)) void fm1_power_settle(void)
{
    volatile int32_t n = 0;
    while (n < 1000)
        n++;
}
#endif
