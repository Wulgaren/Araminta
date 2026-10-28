/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FM-1 supply startup (hal/fm1_power.h): the stock V15 analog setup. Its code
 * lives here, after the audio and UI sources, so adding it never changes the
 * audio ISR's layout (tests/target_budget.py reads the target's loop cost). */
#include "../hal/fm1_power_impl.h"

uint8_t fm1_power_ready;
uint8_t fm1_power_rails[4];                     /* VDC14 / SYSVDD before, then after (ABOUT) */

/* Same field replacement as P33_CON_SET. Never turn a timed-out read's dummy
 * byte into an analog-register write. A failed transfer stops the sequence. */
static int fm1_power_field(uint32_t reg, uint32_t start, uint32_t width, uint32_t value)
{
    uint32_t timeouts = fm1_p33_timeouts;
    uint32_t mask = ((1u << width) - 1u) << start;
    uint8_t old = fm1_p33_read(reg);
    if (fm1_p33_timeouts != timeouts)
        return 0;
    fm1_p33_write(reg, (uint8_t)((old & ~mask) | ((value << start) & mask)));
    return fm1_p33_timeouts == timeouts;
}

/* Both builds use the stock supply setup. This is a fixed boot sequence, not
 * a runtime voltage ladder. CPU1 remains held if any transfer or check fails.
 * The stock's unused reads (6, 5, 5) are retained in their original position. */
static int fm1_power_init(void)
{
    uint32_t timeouts = fm1_p33_timeouts;
    uint8_t mode, io, vdc14, sysvdd;
#define FM1_POWER_STEP(op) do { op; if (fm1_p33_timeouts != timeouts) return 0; } while (0)
#define FM1_POWER_FIELD(reg, start, width, value) \
    do { if (!fm1_power_field(reg, start, width, value)) return 0; } while (0)
    FM1_POWER_FIELD(0x00u, 2, 1, 1);             /* LDO13 on during handover */
    fm1_power_settle();
    FM1_POWER_FIELD(0x00u, 3, 1, 1);             /* DCDC13 on */
    fm1_power_settle();
    FM1_POWER_FIELD(0x00u, 2, 1, 0);             /* LDO13 off */
    fm1_power_settle();
    FM1_POWER_STEP(fm1_p33_write(0x01u, 0x20));
    FM1_POWER_STEP(fm1_p33_write(0x05u, 0x40));
    FM1_POWER_STEP(fm1_p33_write(0x06u, 0x0C));
    FM1_POWER_STEP(fm1_p33_write(0x07u, 0xA5));
    FM1_POWER_STEP(fm1_p33_write(0x08u, 0x96));
    FM1_POWER_FIELD(0x09u, 4, 1, 1);             /* SYSVDD drive */
    FM1_POWER_FIELD(0x05u, 0, 3, 4);             /* VDDIOM 3.2 V */
    FM1_POWER_FIELD(0x06u, 0, 3, 7);             /* VDC14 1.60 V */
    FM1_POWER_FIELD(0x05u, 4, 2, 3);             /* VDDIOW 3.2 V */
    FM1_POWER_STEP((void)fm1_p33_read(0x06u));
    FM1_POWER_STEP((void)fm1_p33_read(0x05u));
    FM1_POWER_STEP((void)fm1_p33_read(0x05u));
    FM1_POWER_STEP(fm1_p33_or(0x00u, 0x80));      /* RC_250k_EN */
    FM1_POWER_FIELD(0x16u, 0, 6, 0);             /* analog keep */
    FM1_POWER_STEP(fm1_p33_or(0x02u, 0x40));      /* VDDOK_DIS */
    FM1_POWER_STEP(fm1_p33_and(0x17u, 0xDF));
    FM1_POWER_FIELD(0x09u, 0, 4, 15);            /* SYSVDD 1.38 V, last */
#undef FM1_POWER_FIELD
#undef FM1_POWER_STEP
    /* Additional read-only verification; no retry or speculative rollback. */
    mode = fm1_p33_read(0x00u);
    io = fm1_p33_read(0x05u);
    vdc14 = fm1_p33_read(0x06u);
    sysvdd = fm1_p33_read(0x09u);
    return fm1_p33_timeouts == timeouts && (mode & 0x0Cu) == 0x08u &&
           (io & 0x37u) == 0x34u && (vdc14 & 7u) == 7u && (sysvdd & 15u) == 15u;
}

/* Capture what the boot loader left, run the stock sequence, capture the result.
 * fm1_power_rails is read-only after boot (MENU > ABOUT shows it). */
static int fm1_power_start(void)
{
    fm1_power_rails[0] = (uint8_t)(fm1_p33_read(FM1_PWR_VDC14) & 7u);
    fm1_power_rails[1] = (uint8_t)(fm1_p33_read(FM1_PWR_SYSVDD) & 15u);
    fm1_power_ready = (uint8_t)fm1_power_init();
    fm1_power_rails[2] = (uint8_t)(fm1_p33_read(FM1_PWR_VDC14) & 7u);
    fm1_power_rails[3] = (uint8_t)(fm1_p33_read(FM1_PWR_SYSVDD) & 15u);
    return fm1_power_ready;
}

/* MENU > ABOUT: the supply rails after the stock setup, and what the boot loader left.
 * Self-contained (no libc): the emulator and the host test build this file too. */
static char *pwr_num(char *d, uint32_t v)
{
    if (v >= 10u)
        *d++ = (char)('0' + v / 10u);
    *d++ = (char)('0' + v % 10u);
    return d;
}
static void fm1_power_about(char *b, unsigned n)
{
    char *d = b;
    if (n < 24u) {
        b[0] = 0;
        return;
    }
    if (!fm1_power_ready) {
        const char *s = "RAILS FAIL 1 CORE";
        while (*s)
            *d++ = *s++;
        *d = 0;
        return;
    }
    {   /* RAILS <vdc14>/<sysvdd> (<before>/<before>) */
        const char *s = "RAILS ";
        while (*s)
            *d++ = *s++;
    }
    d = pwr_num(d, fm1_power_rails[2]);
    *d++ = '/';
    d = pwr_num(d, fm1_power_rails[3]);
    *d++ = ' ';
    *d++ = '(';
    d = pwr_num(d, fm1_power_rails[0]);
    *d++ = '/';
    d = pwr_num(d, fm1_power_rails[1]);
    *d++ = ')';
    *d = 0;
}
