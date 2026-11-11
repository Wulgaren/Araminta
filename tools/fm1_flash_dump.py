#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Back up the FM-1's flash over the chip's ROM UBOOT, and keep the two SDK "VM" areas.

  fm1_flash_dump.py --out DIR
  fm1_flash_dump.py --verify IMAGE.bin [--out DIR]

Araminta claims the four sectors at 0x93000..0x97000 (the flash/lane work). They are the
first 16 KiB of the stock firmware's declared SDK VM region, and on some units stock V15
formats a VM there, which holds the per-device radio calibration (records 106 107 108 187).
Araminta does not use the radio, but a unit that keeps its VM there loses that calibration
when Araminta writes the sectors, and M-VAVE's own updater does not put it back: it writes
only [0x4000, 0x93000).

So RUN THIS BEFORE INSTALLING A BUILD THAT CLAIMS THE GAP, and keep the files it writes.

What it saves (--out DIR), all verified before it returns:
  full-flash.bin            the whole 1 MiB part, byte for byte
  vm-gap.bin                0x93000..0x97000 (16 KiB: two 8 KiB ping-pong areas)
  vm-e8000.bin              0x0E8000..0x0E9000 (4 KiB: where one measured FM-1 keeps it)
  backup.json               sizes, SHA-256 of each file, and each VM area's verdict

Each VM area is checked: the magic 55 AA AA 55, then a walk of the packed records where
every record's check byte (the low byte of CRC-16/XMODEM over its data) must hold, and
record 106 (2 bytes of crystal trims) must be present. 187's own inner CRC is checked when
187 is there. The verdict is written per area:
  usable         a live VM: keep the file, this is the calibration
  empty          all 0xFF: nothing to lose, the gap is free on this unit
  unrecognised   data but not a parsable VM: keep the file anyway, do not trust it

The USB side needs PyUSB and libusb, and the AC79 SDK's RAM helper (wl82loader.bin, fetched
by tools/get_sdk.sh). The helper is loaded into RAM, never written to flash. The USB
BOT/LoaderV2 framing and the 0xFB/0xFC/0xFD command set are the ones FM-1-transporter
documents; this file only reads flash, it has no write path.

  tools/fm1_flash_dump.py --out ~/fm1-backup-2026-11-11
"""
import argparse
import binascii
import hashlib
import json
import os
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path

FLASH_SIZE = 0x100000
GAP_LO, GAP_HI = 0x93000, 0x97000
VM_HW_LO, VM_HW_HI = 0xE8000, 0xE9000
VM_MAGIC = bytes([0x55, 0xAA, 0xAA, 0x55])
VM_AREA_NAMES = {GAP_LO: "vm-gap.bin", VM_HW_LO: "vm-e8000.bin"}

LOADER_SHA256 = "d9de9f1c81a3bb52998e5bced678f0ca851223014a7d43965b03f0719a44eb55"
LOADER_ADDRESS = 0x1C02000
VID, PID = 0x4C4A, 0x8057


class DumpError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise DumpError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


# ---------------------------------------------------------------- the VM format ---

def crc16_xmodem(data):
    """CRC-16/XMODEM (poly 0x1021, init 0, no reflection, no final XOR)."""
    return binascii.crc_hqx(data, 0)


def vm_records(area):
    """Parse one VM area -> {id: data} of the newest valid record per id, or None if it is not a VM.

    Area = magic, then packed records from +4. A record header is four bytes:
      [check = low byte of CRC-16/XMODEM over the data] [id 7:0] [id 11:8 | len 3:0 << 4] [len 11:4]
    The log ends at the first header whose check does not hold (an erased header always fails)
    or at a record that runs past the area. The last valid record of an id wins.
    """
    if len(area) < 4 or bytes(area[:4]) != VM_MAGIC:
        return None
    out, pos = {}, 4
    while pos + 4 <= len(area):
        check = area[pos]
        rid = area[pos + 1] | ((area[pos + 2] & 0x0F) << 8)
        rlen = (area[pos + 2] >> 4) | (area[pos + 3] << 4)
        if rlen == 0 or pos + 4 + rlen > len(area):
            break
        data = bytes(area[pos + 4:pos + 4 + rlen])
        if (crc16_xmodem(data) & 0xFF) != check:
            break
        out[rid] = data
        pos += 4 + rlen
    return out


def vm_verdict(area):
    """-> (verdict, detail). See the module docstring for what each verdict means."""
    if not area:
        return "absent", "no data at this offset"
    if all(b == 0xFF for b in area):
        return "empty", "all 0xFF: nothing stored here on this unit"
    recs = vm_records(area)
    if recs is None:
        return "unrecognised", "no 55 AA AA 55 magic: data, but not an SDK VM"
    if 106 not in recs:
        return "unrecognised", "a VM with no record 106 (crystal trims)"
    if len(recs[106]) != 2:
        return "unrecognised", "record 106 is %d B, expected 2" % len(recs[106])
    if 187 in recs:
        r = recs[187]
        if len(r) < 66:
            return "unrecognised", "record 187 is %d B, expected 68" % len(r)
        inner = r[64] | (r[65] << 8)
        if crc16_xmodem(r[:64]) != inner:
            return "unrecognised", "record 187's inner CRC does not hold"
    ids = " ".join(str(i) for i in sorted(recs))
    return "usable", "records %s" % ids


def extract_vm(image):
    """-> list of per-area dicts (offset, length, name, bytes, verdict, detail)."""
    out = []
    for lo, name in sorted(VM_AREA_NAMES.items()):
        hi = GAP_HI if lo == GAP_LO else VM_HW_HI
        require(hi <= len(image), "image is %d B, too short for the area at %#x" % (len(image), lo))
        area = bytes(image[lo:hi])
        verdict, detail = vm_verdict(area)
        out.append({"offset": lo, "length": hi - lo, "name": name, "bytes": area,
                    "verdict": verdict, "detail": detail})
    return out


def describe(image):
    """The JSON report for an image, without the file paths (the caller adds those)."""
    areas = extract_vm(image)
    return {
        "image_size": len(image),
        "image_sha256": sha256(image),
        "vm_areas": [{k: v for k, v in a.items() if k != "bytes"} for a in areas],
        "empty_gap": next(a["verdict"] == "empty" for a in areas if a["offset"] == GAP_LO),
    }, areas


# ---------------------------------------------------------------- files ---

def durable_write(path, data):
    """Write a new file only: never replace an earlier backup. Sync, then read back."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    require(path.read_bytes() == data, "read-back failed: %s" % path)
    return path


def save_image(image, out_dir, extra=None):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    report, areas = describe(image)
    written = [durable_write(out / "full-flash.bin", image)]
    for a in areas:
        written.append(durable_write(out / a["name"], a["bytes"]))
    report["files"] = [{"path": str(p.name), "size": p.stat().st_size, "sha256": sha256(p.read_bytes())}
                       for p in written]
    report["saved"] = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    if extra:
        report.update(extra)
    durable_write(out / "backup.json", (json.dumps(report, indent=2) + "\n").encode())
    return report


# ---------------------------------------------------------------- USB (ROM UBOOT) ---

def sdk_loader():
    """The AC79 SDK's RAM recovery helper, as tools/get_sdk.sh leaves it."""
    path = Path(__file__).resolve().parent.parent / "build" / "deps" / "ac79" / "cpu" / "wl82" / "tools" / "wl82loader.bin"
    require(path.is_file(), "%s: not found. Run tools/get_sdk.sh once." % path)
    blob = path.read_bytes()
    require(sha256(blob) == LOADER_SHA256,
            "%s: SHA-256 %s, expected %s (use the unmodified SDK file)" % (path, sha256(blob), LOADER_SHA256))
    return blob


class Link:
    """USB BOT to the ROM's LoaderV2. Read-only: the command set here cannot erase or program."""

    def __init__(self, wait=15):
        try:
            import usb.core
            import usb.util
            import usb.backend.libusb1
        except ImportError as error:
            raise DumpError("PyUSB is not installed: python3 -m pip install pyusb") from error
        self.util = usb.util
        import ctypes.util
        name = ("libusb-1.0.dll" if sys.platform == "win32"
                else "libusb-1.0.dylib" if sys.platform == "darwin" else "libusb-1.0.so.0")
        lib = ctypes.util.find_library("usb-1.0")
        backend = usb.backend.libusb1.get_backend(find_library=lambda _: lib or name)
        require(backend is not None, "libusb not found (brew install libusb)")
        print("Waiting for the FM-1 in ROM UBOOT (%04x:%04x)..." % (VID, PID), flush=True)
        import time
        deadline = time.monotonic() + wait
        while True:
            devs = list(usb.core.find(find_all=True, idVendor=VID, idProduct=PID, backend=backend))
            require(len(devs) <= 1, "more than one ROM device: connect a single FM-1")
            if devs:
                self.dev = devs[0]
                break
            require(time.monotonic() < deadline,
                    "no ROM UBOOT device. Put the FM-1 in ROM UBOOT (not the update loader) and retry")
            time.sleep(0.25)
        self.tag = 0
        usb.util.claim_interface(self.dev, 0)

    def transfer(self, cdb, incoming=0, outgoing=None):
        self.tag = self.tag % 0xFFFFFFFF + 1
        size = incoming if outgoing is None else len(outgoing)
        cbw = struct.pack("<IIIBBB", 0x43425355, self.tag, size, 0x80 if incoming else 0, 0, len(cdb))
        cbw += cdb + bytes(16 - len(cdb))
        require(self.dev.write(1, cbw, timeout=5000) == 31, "short USB command write")
        if outgoing is not None:
            require(self.dev.write(1, outgoing, timeout=5000) == len(outgoing), "short USB data write")
        data = bytes(self.dev.read(0x81, incoming, timeout=5000)) if incoming else b""
        csw = bytes(self.dev.read(0x81, 13, timeout=5000))
        require(len(csw) == 13, "short USB status response")
        signature, tag, residue, status = struct.unpack("<IIIB", csw)
        require(signature == 0x53425355 and tag == self.tag and not status and not residue,
                "USB command %s failed: status=%d residue=%d" % (cdb[:2].hex(), status, residue))
        require(len(data) == incoming, "short USB data response")
        return data

    def command(self, cmd, args=b"", incoming=None, outgoing=None):
        cdb = cmd.to_bytes(2, "big") + args
        require(len(cdb) <= 16, "command arguments too long")
        cdb += b"\xff" * (16 - len(cdb))
        if outgoing is not None:
            return self.transfer(cdb, outgoing=outgoing)
        if incoming is not None:
            return self.transfer(cdb, incoming=incoming)
        data = self.transfer(cdb, incoming=16)
        require(data[:2] == cmd.to_bytes(2, "big"), "wrong command echo")
        return data[2:]

    def identify(self):
        inquiry = self.transfer(bytes([0x12, 0, 0, 0, 36, 0]), incoming=36)
        require(inquiry[8:16].strip() == b"WL82" and inquiry[16:32].strip() == b"UBOOT1.00",
                "unexpected ROM identity; refusing")
        return {"vendor": "WL82", "product": "UBOOT1.00"}

    def load(self, blob):
        for off in range(0, len(blob), 512):
            chunk = blob[off:off + 512]
            params = struct.pack(">IH", LOADER_ADDRESS + off, len(chunk)) + b"\0" + struct.pack("<H", crc16_xmodem(chunk))
            self.command(0xFB06, params, outgoing=chunk)
        self.command(0xFB08, struct.pack(">IH", LOADER_ADDRESS, 1))
        self.command(0xFC09, struct.pack(">I", 0xAC6900))       # unlock; the key itself is not needed to read
        info = self.command(0xFC0A)
        require(info[0] == 3, "unexpected flash type; refusing")

    def read_flash(self, address, length):
        require(0 <= address <= FLASH_SIZE and 0 <= length <= FLASH_SIZE - address, "flash read out of bounds")
        out = bytearray()
        for off in range(0, length, 512):
            n = min(512, length - off)
            out += self.command(0xFD05, struct.pack(">IH", address + off, n), incoming=n)
            if length == FLASH_SIZE and off % 0x20000 == 0:
                print("Reading flash: %d%%" % (off * 100 // length), flush=True)
        return bytes(out)

    def close(self):
        self.util.dispose_resources(self.dev)


def dump(out_dir, wait):
    loader = sdk_loader()
    link = Link(wait)
    try:
        ident = link.identify()
        print("Connected to %s / %s. Loading the RAM helper..." % (ident["vendor"], ident["product"]), flush=True)
        link.load(loader)
        image = link.read_flash(0, FLASH_SIZE)
    finally:
        link.close()
    require(len(image) == FLASH_SIZE, "incomplete flash read (%d B); nothing saved" % len(image))
    return save_image(image, out_dir, {"device": ident})


def report_human(report):
    print("full-flash.bin  %d B  sha256 %s" % (report["image_size"], report["image_sha256"]))
    for a in report["vm_areas"]:
        print("  %#08x..%#08x  %-14s %-12s %s"
              % (a["offset"], a["offset"] + a["length"], a["name"], a["verdict"], a["detail"]))
    gap = next(a for a in report["vm_areas"] if a["offset"] == GAP_LO)
    if gap["verdict"] == "usable":
        print("\nThis unit keeps a live VM in the gap: the files above are its radio calibration.")
        print("Keep them. The gap is NOT free on this unit.")
    elif gap["verdict"] == "empty":
        print("\nThe gap is empty (0xFF) on this unit: claiming it loses no stored calibration.")
    else:
        print("\nThe gap holds data that is not a parsable VM. Keep the files and do not assume it is free.")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Back up the FM-1 flash over ROM UBOOT (read-only).")
    ap.add_argument("--out", help="directory to write the backup into (must not exist, or be empty)")
    ap.add_argument("--verify", metavar="IMAGE", help="verify and extract from an image already on disk")
    ap.add_argument("--wait", type=float, default=15, help="seconds to wait for the ROM device")
    a = ap.parse_args(argv)
    try:
        if a.out:
            require(not (Path(a.out) / "full-flash.bin").exists(),
                    "%s already holds a backup; keep it and use a new directory" % a.out)
        if a.verify:
            image = Path(a.verify).read_bytes()
            require(len(image) == FLASH_SIZE, "%s is %d B, expected %d" % (a.verify, len(image), FLASH_SIZE))
            if a.out:
                report = save_image(image, a.out)
            else:
                report, _ = describe(image)
            report_human(report)
            return 0
        require(a.out, "give --out DIR (or --verify IMAGE)")
        report = dump(a.out, a.wait)
        report_human(report)
        print("\nSaved to %s" % a.out)
        return 0
    except FileExistsError as e:
        print("fm1_flash_dump: %s already exists; keep it and use a new directory" % e.filename, file=sys.stderr)
        return 1
    except DumpError as e:
        print("fm1_flash_dump: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
