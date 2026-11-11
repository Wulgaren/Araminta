#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Host test of tools/fm1_flash_dump.py: the VM verdicts and the extraction, no hardware.

The backup tool is the only way back to a unit's factory radio calibration once a build claims
the gap at 0x93000..0x97000, so its verdicts must be right before anything is written there.
The fixtures are the real captured areas:
  FM1_E8   an FM-1's VM at 0x0E8000, 4 KiB, measured 2026-10-08 (Melodee's docs/BLE-HW-FACTS.md)
  V15_A    stock V15's VM area A at 0x093000 in the emulator, 8 KiB, after a first boot
Both are a magic and a record log; the checks below drive every branch of vm_verdict.
  tests/flash_dump_test.py
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import fm1_flash_dump as D                                        # noqa: E402

bad = 0


def check(what, ok, detail=""):
    global bad
    print("dump: %-70s %s%s" % (what, "ok" if ok else "FAIL", "" if ok else "  " + detail))
    if not ok:
        bad += 1


# the FM-1's VM at 0xE8000, 4 KiB mirrored after its log ends (Melodee, docs/BLE-HW-FACTS.md §14)
FM1_E8 = (
    "55aa aa55 916a 2000 0b0b 2c6b 7000 0107 0407 0b01 0722 bb40 04fa 0300 001b 0000 "
    "00ff 0300 0000 0000 00fc ffff ff04 0000 0046 2fff fc00 fd00 002c 0000 005c 0000 "
    "00ff ff7f 39ff ff7f 3950 00ea 024a 00f0 0208 009e 022c 025c 028f 5a00 0000 6c40 "
    "0100 0000 0000 0000 0000 0000 0000 0000 0000 0000 0097 71e0 0006 1a00 00ff ffff "
    "ffff ffff ffff 5a6d 2002 a155 4386 0c87 0eab e175 5dba c386 bb76 ecd8 3b76 eda9 "
    "53bb 6ac8 8d1a 2851 be7d faf5 ffff ff"
)
# stock V15's VM area A at 0x93000 in the emulator (8 KiB area, log ends early)
V15_A = (
    "55aa aa55 916a 2000 0b0b 2c6b 7000 0107 0407 0b01 07ea bb40 0400 0000 0000 0000 "
    "0000 0000 0000 0000 0000 0000 0000 0000 0040 4000 0000 0000 0000 0000 0000 0000 "
    "00ff ffff ffff ffff ff00 0200 0200 0200 0200 0200 0200 0200 0235 1d00 0000 6c40 "
    "0100 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0097 71e0 0006 1a00 00ff "
    "ffff ffff ffff ffff ffcc 6d20 02a1 555b 5b78 78a6 a660 6060 fefe fbfb 2929 3702 "
    "02ac ace4 e430 3030 2727 5656 c8c8 4646 fdff ffff"
)


def blob(text, size):
    """Hex text -> `size` bytes of the area, 0xFF after the text (an erased tail)."""
    data = bytes.fromhex("".join(text.split()))
    assert len(data) <= size, (len(data), size)
    return data + b"\xFF" * (size - len(data))


def area(text, size=0x2000):
    return blob(text, size)


def verdict(a):
    return D.vm_verdict(a)[0]


def main():
    e8 = area(FM1_E8, 0x1000)
    a8 = area(V15_A, 0x2000)

    # ---- the measured FM-1 VM: usable, and the records the caller can rely on
    recs = D.vm_records(e8)
    check("FM1_E8 parses as a VM", recs is not None)
    check("FM1_E8 holds record 106 at 2 bytes", recs is not None and len(recs.get(106, b"")) == 2)
    check("FM1_E8 verdict is usable", verdict(e8) == "usable")
    check("FM1_E8's record 187 inner CRC holds",
          recs is not None and 187 in recs and
          D.crc16_xmodem(recs[187][:64]) == (recs[187][64] | (recs[187][65] << 8)))

    # ---- stock V15's area A: also a usable VM (both halves of the gap can be live)
    check("V15_A parses as a VM with record 106", (D.vm_records(a8) or {}).get(106) is not None)
    check("V15_A verdict is usable", verdict(a8) == "usable")

    # ---- empty: all 0xFF, nothing to lose
    check("all-0xFF is empty", verdict(b"\xFF" * 0x1000) == "empty")
    check("an empty area has no verdict other than empty", verdict(b"\xFF" * 4) == "empty")

    # ---- not a VM at all: no magic
    check("no magic is unrecognised", verdict(b"\x00" * 0x1000) == "unrecognised")
    check("random data is unrecognised", verdict(bytes(range(256)) * 4) == "unrecognised")

    # ---- magic but the first record's check byte is wrong: the log is empty, so no 106
    broken = bytearray(e8)
    broken[4] ^= 0xFF
    check("a broken first check byte is unrecognised (no 106)", verdict(bytes(broken)) == "unrecognised")

    # ---- magic, a good 106, but a corrupted 187: 187's inner CRC must reject it.
    # Built here rather than mutated in the fixture, so the case does not depend on offsets.
    def rec(rid, data):
        return bytes([D.crc16_xmodem(data) & 0xFF, rid & 0xFF,
                      ((rid >> 8) & 0x0F) | ((len(data) & 0x0F) << 4),
                      (len(data) >> 4) & 0xFF]) + data

    good187 = bytes(64) + struct.pack("<H", D.crc16_xmodem(bytes(64))) + b"\x00\x00"
    bad187 = bytes(64) + struct.pack("<H", (D.crc16_xmodem(bytes(64)) + 1) & 0xFFFF) + b"\x00\x00"
    base = bytes([0x55, 0xAA, 0xAA, 0x55]) + rec(106, b"\x0b\x0b")
    check("a synthetic 106+187 with a good inner CRC is usable",
          D.vm_verdict(base + rec(187, good187))[0] == "usable")
    v, detail = D.vm_verdict(base + rec(187, bad187))
    check("a corrupted 187 payload is rejected", v == "unrecognised" and "187" in detail, detail)

    # ---- a truncated record: length runs past the area
    trunc = bytes([0x55, 0xAA, 0xAA, 0x55]) + bytes([0x00, 106, 0xF0, 0xFF]) + b"\x00" * 12
    check("a record past the end of the area ends the walk", D.vm_records(trunc) == {})

    # ---- the walk keeps the LAST valid record of an id
    two = bytes([0x55, 0xAA, 0xAA, 0x55]) + rec(106, b"\x01\x02") + rec(106, b"\x03\x04")
    check("the last valid record of an id wins", (D.vm_records(two) or {}).get(106) == b"\x03\x04")

    # ---- extraction: both areas, at the right offsets and lengths
    img = bytearray(b"\xFF" * D.FLASH_SIZE)
    img[D.GAP_LO:D.GAP_LO + len(a8)] = a8
    img[D.VM_HW_LO:D.VM_HW_LO + len(e8)] = e8
    report, areas = D.describe(bytes(img))
    by = {a["offset"]: a for a in areas}
    check("extraction covers the gap 0x93000..0x97000",
          by[D.GAP_LO]["length"] == 0x4000 and by[D.GAP_LO]["name"] == "vm-gap.bin")
    check("extraction covers 0xE8000..0xE9000",
          by[D.VM_HW_LO]["length"] == 0x1000 and by[D.VM_HW_LO]["name"] == "vm-e8000.bin")
    check("the gap's first 8 KiB reads as the VM it holds", by[D.GAP_LO]["verdict"] == "usable")
    check("the 0xE8000 area reads as usable", by[D.VM_HW_LO]["verdict"] == "usable")
    check("the image digest is the image's", report["image_sha256"] == D.sha256(bytes(img)))
    check("empty_gap is False when the gap holds a VM", report["empty_gap"] is False)

    # ---- a unit whose VM is at 0xE8000 only: the gap is empty, nothing to lose
    img2 = bytearray(b"\xFF" * D.FLASH_SIZE)
    img2[D.VM_HW_LO:D.VM_HW_LO + len(e8)] = e8
    report2, _ = D.describe(bytes(img2))
    check("a unit with its VM at 0xE8000 reports an empty gap", report2["empty_gap"] is True)

    print("flash dump test %s" % ("FAILED" if bad else "passed"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
