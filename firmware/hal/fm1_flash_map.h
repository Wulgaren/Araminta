/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 flash map and the write windows the RAM driver may touch. Kept apart from fm1_flash.h, which is the register
 * layer and needs the target: these are plain arithmetic on addresses, so tests/flash_map_test.c compiles them on the
 * host and checks every window the firmware relies on.
 *
 * Ranges, low to high (the AC79's 1 MiB part):
 *   0x004000..0x093000  the app area (the loader writes only this), fm1pkg_make.py APP_SLOT
 *   0x093000..0x097000  the gap: four sectors between the app and the store. Autosave B takes the first two
 *                       (0x93000..0x94FFF); 0x95000..0x96FFF hold user-preset bank 5 (A/B). Readable through
 *                       the plaintext window (fl_plain_window_init), Felucca's since 3.10
 *   0x097000..0x0E0000  the main store: projects, user samples (80 KiB x 3), user presets
 *   0x0E0000..0x0E5000  the update loader's staging (ota.c), never a storage object
 *   0x0E5000..0x0E7000  autosave A (two sectors); 0x0E7000..0x0E8000 the FM6 voice bank
 *   0x0E8000..0x0F4000  stock V15 (VM / BTIF / USR voices start): never Araminta's to erase
 *   0x0F4000..0x0FA000  Araminta reclaim: user banks (F5000..F7000), FM6 banks (F8000/F9000/F4000)
 *   0x0FA000..0x0FC000  stock USR settings tail: never Araminta's
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
#define FL_AUTO_LO      0x000E5000u                /* 3.10: autosave A, two sectors (B is in the gap: storage.c) */
#define FL_AUTO_HI      0x000E7000u
#define FL_VBANK_LO     0x000E7000u                /* 1.4.1: the FM6 voice bank (fm6_vbank.c), the sector above A */
#define FL_VBANK_HI     0x000E8000u                /* (below the stock OTA's VM backup at 0xE8000: the app never
                                                 * writes it; the update loader only clears an update record's tag
                                                 * in that sector, never erases it: ldr_core.c) */
#define FL_RECLAIM_LO   0x000F4000u                /* user banks F5–F7 + FM6 F8/F9/F4 (stock V15 ignores these) */
#define FL_RECLAIM_HI   0x000FA000u
#define FL_GLOB_LO      0x000FC000u                /* Felucca superblock / globals */
#define FL_GLOB_HI      0x000FF000u

/* [off, off + n) inside [lo, hi), without wrapping: off + n can overflow, and
 * the 1 MiB part ignores the high address bits, so a wrapped range lands low. */
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
/* Felucca's own store (projects, user samples; settings; the autosave and the FM6 voice bank: one range), the
 * four sectors the app area leaves free below it, and the reclaim band stock V15 leaves alone (F4000..F9FFF).
 *
 * The gap's sectors hold an update record in their last 256 B while the SPL scans them ("4K boundary - 256",
 * ldr_core.c). Erasing a gap sector clears that tail, which is safe: a record exists only while an update is staged,
 * and Felucca does not run then (ldr_core.c clears it before booting the new app). The tail is therefore written
 * like any other byte; what must NOT happen is a record being written into a gap sector by something else, which no
 * code does. */
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || \
                             FL_IN(off, n, FL_AUTO_LO, FL_VBANK_HI) || FL_IN(off, n, FL_GAP_LO, FL_GAP_HI) || \
                             FL_IN(off, n, FL_RECLAIM_LO, FL_RECLAIM_HI))
/* Where the RAM driver may erase / program. The app build allows only its own data regions (the store and the gap);
 * the update loader (firmware/loader) defines its own window over the app area and the OTA staging before including
 * this header. */
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif

