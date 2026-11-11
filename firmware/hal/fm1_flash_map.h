/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 flash map and the write windows the RAM driver may touch. Kept apart from fm1_flash.h, which is the register
 * layer and needs the target: these are plain arithmetic on addresses, so tests/flash_map_test.c compiles them on the
 * host and checks every window the firmware relies on.
 *
 * Ranges, low to high (the AC79's 1 MiB part):
 *   0x004000..0x093000  the app area (the loader writes only this), fm1pkg_make.py APP_SLOT
 *   0x093000..0x097000  the gap: four sectors between the app and the store. Nobody's until 3.10, readable through
 *                       the plaintext window (fl_plain_window_init), now Felucca's own
 *   0x097000..0x0E0000  the main store: projects, user samples, user presets
 *   0x0E0000..0x0E5000  the update loader's staging (ota.c), never a storage object
 *   0x0E5000..0x0E8000  the autosave A/B and the FM6 voice bank
 *   0x0FC000..0x0FF000  settings (the superblock / globals)
 */
#pragma once
#include <stdint.h>

#define FL_APP_LO       0x00004000u                /* the loader's window starts after the flash head */
#define FL_APP_HI       0x00093000u                /* fm1pkg_make.py FLASH_SIZE: the app area's end */
#define FL_GAP_LO       0x00093000u                /* four free sectors the app area leaves: 16,384 B */
#define FL_GAP_HI       0x00097000u
#define FL_DATA_LO      0x00097000u                /* Felucca main store */
#define FL_DATA_HI      0x000E0000u
#define FL_OTA_LO       0x000E0000u                /* M-UPGRADE loader staging, ota.c */
#define FL_OTA_HI       0x000E5000u
#define FL_AUTO_LO      0x000E5000u                /* 1.2: the autosave (storage.c OBJ_AUTOSAVE), A/B, just above */
#define FL_AUTO_HI      0x000E7000u                /* the OTA staging */
#define FL_VBANK_LO     0x000E7000u                /* 1.4.1: the FM6 voice bank (fm6_vbank.c), the sector above */
#define FL_VBANK_HI     0x000E8000u                /* (below the stock OTA's VM backup, 0xE8000: never written) */
#define FL_GLOB_LO      0x000FC000u                /* Felucca superblock / globals */
#define FL_GLOB_HI      0x000FF000u

/* [off, off + n) inside [lo, hi), without wrapping: off + n can overflow, and
 * the 1 MiB part ignores the high address bits, so a wrapped range lands low. */
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
/* Felucca's own store (projects, user samples; settings; the autosave and the FM6 voice bank: one range), and the
 * four sectors the app area leaves free below it.
 *
 * The gap's sectors hold an update record in their last 256 B while the SPL scans them ("4K boundary - 256",
 * ldr_core.c). Erasing a gap sector clears that tail, which is safe: a record exists only while an update is staged,
 * and Felucca does not run then (ldr_core.c clears it before booting the new app). The tail is therefore written
 * like any other byte; what must NOT happen is a record being written into a gap sector by something else, which no
 * code does. */
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || \
                             FL_IN(off, n, FL_AUTO_LO, FL_VBANK_HI) || FL_IN(off, n, FL_GAP_LO, FL_GAP_HI))
/* Where the RAM driver may erase / program. The app build allows only its own data regions (the store and the gap);
 * the update loader (firmware/loader) defines its own window over the app area and the OTA staging before including
 * this header. */
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif

