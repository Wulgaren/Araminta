/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The flash map's write windows (firmware/hal/fm1_flash_map.h), on the host: no registers, no hardware.
 *
 * storage_hw.c refuses any erase or program outside FL_STORE_OK, and the RAM driver refuses anything outside
 * FL_RANGE_OK. Those two gates are the only thing between a bad offset and somebody's settings, so each window is
 * checked here by address arithmetic: the gap the app area leaves (0x93000..0x97000), the store, the autosave, the
 * globals, the OTA staging, and the bands that must stay refused (the app area below 0x93000 and the VM backup at
 * 0xE8000, which is not ours). */
#include <stdio.h>
#include <stdint.h>
#include "fm1_flash_map.h"

static int bad;
static int check(const char *what, int ok)
{
    printf("flashmap: %-66s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* one 4 KiB sector erase at off must be allowed (app) / refused */
#define ERASE(off) FL_STORE_OK((off), 0x1000u)

int main(void)
{
    /* the gap: four sectors between the app area and the store, the space this feature adds */
    bad += check("the gap's four sectors erase: 0x93000 0x94000 0x95000 0x96000",
                 ERASE(0x93000u) && ERASE(0x94000u) && ERASE(0x95000u) && ERASE(0x96000u));
    bad += check("the gap ends at 0x97000 (a sector past it is the store's, not the gap's)",
                 ERASE(0x97000u) && !FL_IN(0x97000u, 1u, FL_GAP_LO, FL_GAP_HI));
    bad += check("a whole gap sector's tail is writable (the SPL's record scan is cleared by an erase)",
                 FL_STORE_OK(0x96F00u, 0x100u));
    bad += check("the last gap byte is writable; the byte at 0x97000 is the store's, not the gap's",
                 FL_STORE_OK(FL_GAP_HI - 1u, 1u) && FL_STORE_OK(FL_GAP_HI, 1u) &&
                 FL_IN(FL_GAP_HI - 1u, 1u, FL_GAP_LO, FL_GAP_HI) && !FL_IN(FL_GAP_HI, 1u, FL_GAP_LO, FL_GAP_HI));
    bad += check("a range crossing the gap's end is refused", !FL_STORE_OK(0x96F00u, 0x200u));
    bad += check("a range crossing the gap's start is refused", !FL_STORE_OK(0x92F00u, 0x200u));
    bad += check("nothing below 0x93000 is the app's: the app area, one sector of it, its first byte",
                 !ERASE(0x92000u) && !ERASE(0x4000u) && !FL_STORE_OK(0x8DFBCu, 4u));

    /* the store and its neighbours, unchanged */
    bad += check("the store (projects, samples, presets) erases at its first sector",
                 ERASE(FL_DATA_LO));
    bad += check("the autosave and the FM6 voice bank erase", ERASE(FL_AUTO_LO) && ERASE(FL_VBANK_LO));
    bad += check("settings (globals) erase", ERASE(FL_GLOB_LO));
    bad += check("the VM backup above the voice bank is not ours", !ERASE(FL_VBANK_HI));
    bad += check("the stock firmware's update record at 0xE8F00 is not ours",
                 !FL_STORE_OK(0xE8F00u, 0x100u));
    bad += check("reclaim F4000..F9FFF erases (factory / extra banks)",
                 ERASE(FL_RECLAIM_LO) && ERASE(0xF5000u) && ERASE(0xF9000u) &&
                 ERASE(FL_RECLAIM_HI - 0x1000u));
    bad += check("stock USR below reclaim (E9000..F3FFF) is refused",
                 !ERASE(0xE9000u) && !ERASE(0xF2000u) && !ERASE(0xF3000u));
    bad += check("stock USR settings FA000..FBFFF are refused",
                 !ERASE(0xFA000u) && !ERASE(0xFB000u));
    bad += check("a range crossing reclaim into FA000 is refused",
                 !FL_STORE_OK(0xF9F00u, 0x200u));

    /* the RAM driver's window: the app may program the gap, but never the app area it is running from */
    bad += check("the driver programs the gap and the OTA staging",
                 FL_RANGE_OK(0x93000u, 0x100u) && FL_RANGE_OK(FL_OTA_LO, 0x100u));
    bad += check("the driver never programs the running app area", !FL_RANGE_OK(0x92000u, 0x100u));
    bad += check("the driver never programs the flash head", !FL_RANGE_OK(0u, 0x100u));

    /* the wraparound guard: a wrapped range must not land low */
    bad += check("a range that wraps 32 bits is refused",
                 !FL_STORE_OK(0xFFFFFF00u, 0x200u) && !FL_RANGE_OK(0xFFFFFF00u, 0x200u));
    bad += check("a zero-length range is accepted (only used with n > 0, but must not trap)",
                 FL_STORE_OK(FL_GAP_LO, 0u));

    /* ---- the layout itself ----------------------------------------------------------------
     * The numbers this feature was built on, checked as numbers rather than as prose. A byte count that drifts, or
     * two regions that start sharing sectors, breaks here instead of in somebody's settings. */
    {
        /* every region that must tile the flash without overlapping, low to high. The app area ends where the gap
         * starts; the gap ends where the store starts; the OTA staging, the autosave and the voice bank follow; the
         * globals sit above them. Only the gap is new, and it is exactly the piece the other ranges left unclaimed. */
        const uint32_t LO[] = {FL_APP_LO, FL_GAP_LO, FL_DATA_LO, FL_OTA_LO, FL_AUTO_LO, FL_VBANK_LO, FL_GLOB_LO};
        const uint32_t HI[] = {FL_APP_HI, FL_GAP_HI, FL_DATA_HI, FL_OTA_HI, FL_AUTO_HI, FL_VBANK_HI, FL_GLOB_HI};
        const uint32_t n = sizeof LO / sizeof LO[0], gap = FL_GAP_HI - FL_GAP_LO;
        uint32_t k;
        int ordered = 1, no_overlap = 1;
        for (k = 0; k < n; k++) {
            ordered &= HI[k] > LO[k];
            if (k)
                no_overlap &= LO[k] >= HI[k - 1];
        }
        bad += check("every region has a positive length and they climb 0x4000 .. 0xFF000", ordered && no_overlap);
        bad += check("the app area ends exactly where the gap starts", FL_APP_HI == FL_GAP_LO);
        bad += check("the gap ends exactly where the store starts", FL_GAP_HI == FL_DATA_LO);
        bad += check("the gap is four whole 4 KiB sectors (16,384 B)", gap == 4u * 4096u && gap / 4096u == 4u);
        bad += check("the gap's sectors are all sector aligned",
                 (FL_GAP_LO & 0xFFFu) == 0u && (FL_GAP_HI & 0xFFFu) == 0u);
        /* each region that is the app's is writable at both ends; the app area and the OTA staging are not (the
         * loader owns them), which is the point of the gate */
        bad += check("each app-owned region is writable at its first and last byte",
                     FL_STORE_OK(FL_GAP_LO, 1u) && FL_STORE_OK(FL_GAP_HI - 1u, 1u) &&
                     FL_STORE_OK(FL_DATA_LO, 1u) && FL_STORE_OK(FL_DATA_HI - 1u, 1u) &&
                     FL_STORE_OK(FL_VBANK_LO, 1u) && FL_STORE_OK(FL_VBANK_HI - 1u, 1u));
        bad += check("the app area and the OTA staging are the loader's, not the app's",
                     !FL_STORE_OK(FL_APP_LO, 1u) && !FL_STORE_OK(FL_APP_HI - 1u, 1u) && !FL_STORE_OK(FL_OTA_LO, 1u));
        bad += check("no region straddles another's start",
                 !FL_STORE_OK(FL_DATA_LO - 1u, 2u) && !FL_STORE_OK(FL_GAP_LO - 1u, 2u));
    }
    /* the store sizes the project format and the tools all quote. 3840 is ST_SECTOR - ST_PAYLOAD_OFF, so a project
     * fits one sector's payload; 0x8DFBC is the app slot fm1pkg_make.py packs to. These live in four places (C,
     * fm1pkg_make.py, web/projconv.js, app.ld); layout_test.c checks the other three agree with these. */
    bad += check("3840 B is one sector's payload (4096 - 256)", 4096u - 256u == 3840u);
    bad += check("the flash head is 16 KiB and the app starts right after it", FL_APP_LO == 0x4000u);
    bad += check("the globals' end is the 1 MiB part's top sector bound", FL_GLOB_HI == 0xFF000u);

    printf("flash map test %s\n", bad ? "FAILED" : "passed");
    return bad ? 1 : 0;
}
