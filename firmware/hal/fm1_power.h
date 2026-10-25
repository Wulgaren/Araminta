/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FM-1 supply startup, matching the stock V15 analog setup.
 * Ported from Melodee (hal/fm1_power.h, commit 7a51700), GPL-3.0-only.
 * Register order / values: stock app 0x02004F90..0x0200509A, identified against
 * the AC79 SDK __hw_power_init. See BUILDING.md, "The stock supply setup".
 * Call with IRQs off and CPU1 held, after application RAM is initialized.
 *
 * Levels (SDK p33.h): SYSVDD, the core and SRAM rail (isd_config "DVDD"),
 * 0.93 V + 30 mV a step (15: 1.38 V); VDC14 ("DCDC14") 1.25 V + 50 mV a step
 * (7: 1.60 V); VDDIO 2.8 V + 100 mV a step (4: 3.2 V).
 *
 * The implementation is firmware/src/power.c, included after the audio and UI
 * code: its code placement must not move the audio ISR (tests/target_budget.txt).
 */
#pragma once
#include <stdint.h>
#ifndef FM1_POWER_HOST_TEST
#include "fm1_sys.h"
#endif

/* the analog registers the stock setup reaches */
#define FM1_PWR_VDC14 0x06u
#define FM1_PWR_SYSVDD 0x09u
#define FM1_PWR_VDDIO 0x05u

/* defined in src/power.c, after the audio and UI globals: their addresses must not move */
extern uint8_t fm1_power_ready;
extern uint8_t fm1_power_rails[4];              /* VDC14 / SYSVDD before, then after (ABOUT) */

static int fm1_power_field(uint32_t reg, uint32_t start, uint32_t width, uint32_t value);
static int fm1_power_init(void);
static int fm1_power_start(void);
static void fm1_power_about(char *b, unsigned n);   /* MENU > ABOUT's RAILS line */
