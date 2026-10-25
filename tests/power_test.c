/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Supply boot against the stock V15 transaction trace, including warm starts,
 * failed transfers and incorrect read-back. No physical voltage is simulated.
 * Ported from Melodee (tests/power_test.c, commit 7a51700).
 * Oracle: FM-1_015 SHA256 db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a,
 * app 0x02004F90..0x0200509A, helper 0x01C0108C. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { char op; uint8_t reg, value; } event_t;
static event_t trace[64];
static uint8_t regfile[256];
static uint32_t used, transfers, fail_at, corrupt_at, corrupt_mask, fm1_p33_timeouts;
static int transfer(char op, uint32_t reg, uint8_t value)
{
    assert(used < sizeof trace / sizeof *trace && reg <= 0x17u);
    trace[used++] = (event_t){op, (uint8_t)reg, value};
    if (++transfers == fail_at) {
        fm1_p33_timeouts++;
        return 0;
    }
    return 1;
}
static uint8_t fm1_p33_read(uint32_t reg)
{
    if (!transfer('R', reg, 0)) return 0x5E;
    return regfile[reg] ^ (transfers == corrupt_at ? corrupt_mask : 0u);
}
static void fm1_p33_write(uint32_t reg, uint8_t v)
{
    if (transfer('W', reg, v)) regfile[reg] = v;
}
static void fm1_p33_or(uint32_t reg, uint8_t v)
{
    if (transfer('O', reg, v)) regfile[reg] |= v;
}
static void fm1_p33_and(uint32_t reg, uint8_t v)
{
    if (transfer('A', reg, v)) regfile[reg] &= v;
}
static void fm1_power_settle(void)
{
    assert(used < sizeof trace / sizeof *trace);
    trace[used++] = (event_t){'D', 0, 0};
}
#define FM1_POWER_HOST_TEST 1
#include "../firmware/src/power.c"

static void reset(uint8_t mode)
{
    memset(regfile, 0xAB, sizeof regfile);
    regfile[0] = mode;
    used = transfers = fail_at = corrupt_at = 0;
    corrupt_mask = 1;
    fm1_p33_timeouts = 17;                        /* older unrelated timeouts are allowed */
}

static void stock_trace(uint8_t mode)
{
    /* Resolved bus transactions from V15, plus our four final reads.
     * RMWs preserve the inherited upper bits; the five full analog writes do not. */
    const event_t expected[] = {
        {'R',0,0}, {'W',0,mode|4}, {'D',0,0},
        {'R',0,0}, {'W',0,mode|12}, {'D',0,0},
        {'R',0,0}, {'W',0,(mode|8)&~4}, {'D',0,0},
        {'W',1,0x20}, {'W',5,0x40}, {'W',6,0x0C}, {'W',7,0xA5}, {'W',8,0x96},
        {'R',9,0}, {'W',9,0xBB},
        {'R',5,0}, {'W',5,0x44}, {'R',6,0}, {'W',6,0x0F}, {'R',5,0}, {'W',5,0x74},
        {'R',6,0}, {'R',5,0}, {'R',5,0}, {'O',0,0x80},
        {'R',0x16,0}, {'W',0x16,0x80}, {'O',2,0x40}, {'A',0x17,0xDF},
        {'R',9,0}, {'W',9,0xBF},
        {'R',0,0}, {'R',5,0}, {'R',6,0}, {'R',9,0}
    };
    assert(used == sizeof expected / sizeof *expected);
    for (uint32_t i = 0; i < used; i++) {
        assert(trace[i].op == expected[i].op && trace[i].reg == expected[i].reg);
        assert(trace[i].value == expected[i].value);
    }
    assert((regfile[0] & 0x0Cu) == 8u && (regfile[6] & 7u) == 7u && (regfile[9] & 15u) == 15u);
    for (uint32_t i = 0; i < sizeof regfile; i++)
        if (i != 0 && i != 1 && i != 2 && (i < 5 || i > 9) && i != 0x16 && i != 0x17)
            assert(regfile[i] == 0xAB);           /* no watchdog/reset/RTC writes */
}

int main(void)
{
    reset(4); fm1_power_ready = fm1_power_init();
    assert(fm1_power_ready); stock_trace(4);
    uint32_t count = transfers;
    reset(0x88); assert(fm1_power_init()); stock_trace(0x88); /* stock DCDC state after soft reset */
    for (uint32_t bad = 1; bad <= count; bad++) {
        reset(4); fail_at = bad;
        assert(!fm1_power_init() && fm1_p33_timeouts == 18);
        uint32_t seen = 0;
        for (uint32_t i = 0; i < used; i++) {
            if (trace[i].op == 'D') continue;
            if (++seen > bad) assert(trace[i].op == 'R'); /* never write after a timeout */
        }
    }
    /* A dropped hardware bit despite successful transactions also refuses
     * readiness. Mode corruption uses bit 2 (LDO), selectors use bit 0. */
    for (uint32_t bad = count - 3; bad <= count; bad++) {
        reset(4); corrupt_at = bad;
        if (bad == count - 3) corrupt_mask = 4;
        assert(!fm1_power_init());
    }
    reset(4); fail_at = 1;
    assert(!fm1_power_field(9, 0, 4, 15) && used == 1 && regfile[9] == 0xAB);
    puts("power: stock V15 trace, cold/warm starts, every transfer timeout and rail read-back faults passed");
    return 0;
}
