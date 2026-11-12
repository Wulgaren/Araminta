#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The flash and project layout constants, checked across the languages that carry copies of them.

The same numbers live in C, Python, JavaScript and the linker script, because four different tools need them:
the firmware, the package builder, the web installer / editor, and the linker. Nothing makes them agree, so a
change to one is a silent change to the others until something breaks on a device. This test reads each value
out of its own file and asserts the copies are equal.

It is deliberately about the NUMBERS, not about behaviour: tests/flash_map_test.c covers what the windows mean,
tests/ldr_test.c covers what an update does to the flash. This one covers arithmetic that was quoted in prose
and got wrong: "the spare bytes" that were bits, a slot size that was not divided by the sector size.
  tests/layout_test.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

bad = 0


def check(what, ok, detail=""):
    global bad
    print("layout: %-64s %s%s" % (what, "ok" if ok else "FAIL", "" if ok else "  " + detail))
    if not ok:
        bad += 1


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def grab(rel, pattern, what):
    """one integer from a file; the pattern must match exactly once (a silent second copy is the bug)"""
    hits = re.findall(pattern, read(rel), re.M)
    if len(hits) != 1:
        check("%s: one definition in %s" % (what, rel), False, "found %d" % len(hits))
        return None
    return int(hits[0], 0)


# ---- the app slot: how big app.bin may be (the linker, the package builder, the installer) -------------------
app_py = grab("tools/fm1pkg_make.py", r"^APP_SLOT\s*=\s*(0x[0-9A-Fa-f]+|\d+)", "APP_SLOT")
app_js = grab("web/fm1pkg.js", r"^export const STOCK_APP_SIZE\s*=\s*(0x[0-9A-Fa-f]+|\d+)", "STOCK_APP_SIZE")
app_ld = grab("firmware/app.ld", r"XIP\s*\(rx\)\s*:\s*ORIGIN\s*=\s*0x[0-9A-Fa-f]+,\s*LENGTH\s*=\s*(0x[0-9A-Fa-f]+)",
              "XIP LENGTH")
check("the app slot is the same in the linker, the package builder and the installer",
      app_py is not None and app_py == app_js == app_ld,
      "fm1pkg_make %s, fm1pkg.js %s, app.ld %s" % (app_py, app_js, app_ld))

# ---- the app area's end: where the gap starts ---------------------------------------------------------------
flash_py = grab("tools/fm1pkg_make.py", r"^FLASH_SIZE\s*=\s*(0x[0-9A-Fa-f]+|\d+)", "FLASH_SIZE")
gap_lo = grab("firmware/hal/fm1_flash_map.h", r"^#define FL_GAP_LO\s+(0x[0-9A-Fa-f]+)u", "FL_GAP_LO")
app_hi = grab("firmware/hal/fm1_flash_map.h", r"^#define FL_APP_HI\s+(0x[0-9A-Fa-f]+)u", "FL_APP_HI")
ldr_app_hi = grab("firmware/loader/ldr_core.c", r"^#define LDR_APP_HI\s+(0x[0-9A-Fa-f]+)u", "LDR_APP_HI")
check("the app area ends where the gap and the loader say it does",
      flash_py is not None and flash_py == gap_lo == app_hi == ldr_app_hi,
      "FLASH_SIZE %s, FL_GAP_LO %s, FL_APP_HI %s, LDR_APP_HI %s" % (flash_py, gap_lo, app_hi, ldr_app_hi))
check("the app slot fits inside the app area", app_py is not None and app_py <= app_hi,
      "APP_SLOT %s > FL_APP_HI %s" % (app_py, app_hi))

# ---- the gap, and the store above it --------------------------------------------------------------------
gap_hi = grab("firmware/hal/fm1_flash_map.h", r"^#define FL_GAP_HI\s+(0x[0-9A-Fa-f]+)u", "FL_GAP_HI")
data_lo = grab("firmware/hal/fm1_flash_map.h", r"^#define FL_DATA_LO\s+(0x[0-9A-Fa-f]+)u", "FL_DATA_LO")
ldr_data_lo = grab("firmware/loader/ldr_core.c", r"^#define LDR_DATA_LO\s+(0x[0-9A-Fa-f]+)u", "LDR_DATA_LO")
ldr_gap_lo = grab("firmware/loader/ldr_core.c", r"^#define LDR_GAP_LO\s+(0x[0-9A-Fa-f]+)u", "LDR_GAP_LO")
ldr_gap_hi = grab("firmware/loader/ldr_core.c", r"^#define LDR_GAP_HI\s+(0x[0-9A-Fa-f]+)u", "LDR_GAP_HI")
check("the loader's gap is the map's gap", gap_lo == ldr_gap_lo and gap_hi == ldr_gap_hi,
      "map %s..%s, loader %s..%s" % (gap_lo, gap_hi, ldr_gap_lo, ldr_gap_hi))
check("the gap ends exactly where the store starts", gap_hi == data_lo == ldr_data_lo,
      "FL_GAP_HI %s, FL_DATA_LO %s, LDR_DATA_LO %s" % (gap_hi, data_lo, ldr_data_lo))
if gap_lo is not None and gap_hi is not None:
    n = (gap_hi - gap_lo) // 4096
    check("the gap is a whole number of 4 KiB sectors", (gap_hi - gap_lo) % 4096 == 0,
          "%d B" % (gap_hi - gap_lo))
    check("the gap is four sectors (the number quoted in the notes)", n == 4, "%d sectors" % n)

# ---- the store size the project format assumes ------------------------------------------------------------
# 3.10: projects are now single-copy, two sectors (4096 bytes): storage.c st_sectors() + st_pmax()
proj_c = grab("firmware/src/project.c", r"^#define PROJ_STORE_SIZE\s+(\d+)u", "PROJ_STORE_SIZE")
proj_old_c = grab("firmware/src/project.c", r"^#define PROJ_STORE_OLD\s+(\d+)u", "PROJ_STORE_OLD")
proj_js = grab("web/projconv.js", r"^const PROJ_STORE_SIZE\s*=\s*(\d+)", "PROJ_STORE_SIZE")
sector = grab("firmware/src/storage.c", r"^#define ST_SECTOR\s+(\d+)u", "ST_SECTOR")
payload_off = grab("firmware/src/storage.c", r"^#define ST_PAYLOAD_OFF\s+(\d+)u", "ST_PAYLOAD_OFF")
# the web editor should accept both old (3840) and new (4096) sizes; for now it reads old
check("the web editor reads the old 3840-byte format (the importer handles both)",
      proj_old_c is not None and proj_js == proj_old_c,
      "js %s, PROJ_STORE_OLD %s" % (proj_js, proj_old_c))
# 3.10: project grew from 3840 to 4096 bytes (one sector); the storage spans two sectors (capacity 7936)
check("a project is exactly one sector (4096 bytes), inside its two-sector storage span",
      proj_c is not None and proj_c == sector and proj_c < 2 * sector - payload_off,
      "project %s, sector %s, max capacity %s" % (proj_c, sector, 2 * (sector or 0) - (payload_off or 0)))
check("the project object is a multiple of 4 (the hash is a uint32 at the end)", proj_c % 4 == 0)

# ---- the record size the motion block and the capacity arithmetic use -------------------------------------
check("a motion record is 3 bytes on disk",
      re.search(r"typedef struct \{ uint8_t place, param, value; \} motion_file_event_t;", read("firmware/src/core.h"))
      is not None)
motion_body = grab("firmware/src/core.h", r"^#define MOTION_FILE_MAX_BODY\s+(\d+)u", "MOTION_FILE_MAX_BODY")
motion_app = grab("firmware/src/core.h", r"^#define MOTION_FILE_APPEND_MAX\s+(\d+)u", "MOTION_FILE_APPEND_MAX")
motion_spare = grab("firmware/src/core.h", r"^#define MOTION_FILE_SPARE_MAX\s+(\d+)u", "MOTION_FILE_SPARE_MAX")
# FUN20: 155 = 88 body + 40 append + 27 spare; body stays 88 so DX/FM6 offsets stay frozen
check("MOTION_FILE_MAX: one definition in firmware/src/core.h",
      re.search(r"^#define MOTION_FILE_MAX \(MOTION_FILE_MAX_BODY \+ MOTION_FILE_APPEND_MAX \+ MOTION_FILE_SPARE_MAX\)",
                read("firmware/src/core.h"), re.M) is not None)
check("the in-body motion block is defined as 4 + MOTION_FILE_MAX_BODY x a 3-byte record",
      re.search(r"^#define MOTION_FILE_SIZE \(4u \+ MOTION_FILE_MAX_BODY \* sizeof\(motion_file_event_t\)\)",
                read("firmware/src/core.h"), re.M) is not None)
check("FUN20 motion is 155 records (88 body + 40 append + 27 spare)",
      motion_body == 88 and motion_app == 40 and motion_spare == 27,
      "BODY %s APPEND %s SPARE %s" % (motion_body, motion_app, motion_spare))
if motion_body is not None:
    check("the in-body motion block fits its share of the store (under one sector's payload)",
          4 + motion_body * 3 <= (sector or 0) - (payload_off or 0),
          "%d B for %d records" % (4 + motion_body * 3, motion_body))

print("layout test %s" % ("FAILED" if bad else "passed"))
sys.exit(1 if bad else 0)
