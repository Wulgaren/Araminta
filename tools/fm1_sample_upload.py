#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Load WAV files into a Felucca user sample slot (USR1..USR3) over USB-MIDI.

  fm1_sample_upload.py info
  fm1_sample_upload.py load SLOT NAME file.wav[:ROOT[:LO-HI]] ...   (SLOT 1..3)
  fm1_sample_upload.py erase SLOT
  fm1_sample_upload.py build NAME OUT_PREFIX file.wav[...] ...      (no device: writes OUT_PREFIX.hdr / .bin)

Each file becomes one zone: mono, 22050 Hz, IMA ADPCM (sampleio.py, the same encoder
as the built-in sets and the web editor). ROOT is a MIDI note (default: from the file
name, C4 = 60, else 60); without LO-HI the zones split the keyboard between their roots.
A slot holds 80 KiB (about 7 s at 22050 Hz). Protocol: web/EDITOR_PROTOCOL.md, cmds 11..15.
Needs mido (and a backend such as python-rtmidi) for the device commands.
"""
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402

HDR = [0x7D, 0x46, 0x4C]
RC = {1: "size", 2: "header", 3: "data CRC", 4: "flash", 5: "zones"}


def printable(b):
    """bytes from the device as text for the terminal: printable ASCII only (no escape sequences)"""
    return "".join(chr(c) if 32 <= c < 127 else "?" for c in b)


def slot_info(r):
    """the SMP_INFO reply -> one line per slot; ValueError when it is malformed"""
    try:
        n, i, out = r[0], 2, []
        for k in range(n):
            nz = r[i]
            j = r.index(0, i + 1)
            out.append(f"USR{k + 1}: {'empty' if not nz else f'{nz} zones, {printable(r[i + 1:j])}, {r[j + 1]} KiB'}")
            i = j + 2
        return out
    except (IndexError, ValueError):
        raise ValueError("malformed SMP_INFO reply from the device") from None


def pack7_groups(b):
    """groups of up to 7 bytes, each preceded by their top bits (editor.c ed_unpack7).

    Named for the format: fm1_install.py's pack7_bits is a different packing (ota.c ota_pack7)."""
    out = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        out += [x & 0x7F for x in g]
    return out


class Link:
    def __init__(self):
        try:
            import mido
        except ImportError:
            sys.exit("needs mido: pip install mido python-rtmidi")
        self.mido = mido
        def is_fm1(n):
            return re.search(r"felucca|fm-?1", n, re.I)
        outs = [n for n in mido.get_output_names() if is_fm1(n)]
        ins = [n for n in mido.get_input_names() if is_fm1(n)]
        if not outs or not ins:
            sys.exit("no MIDI port named FM-1 or Felucca: connect the FM-1 by USB")
        self.o = mido.open_output(outs[0])
        self.i = mido.open_input(ins[0])
        for _ in range(3):                          # the first frame after opening the port can be lost
            try:
                self.req(1, [], 1.0)
                break
            except TimeoutError:
                pass

    def req(self, cmd, args, timeout=3.0):
        for _ in self.i.iter_pending():
            pass
        self.o.send(self.mido.Message("sysex", data=HDR + [cmd] + list(args)))
        t0 = time.time()
        while time.time() - t0 < timeout:
            for m in self.i.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HDR + [cmd]:
                    return list(m.data[4:])
            time.sleep(0.002)
        raise TimeoutError(f"no reply to cmd {cmd}")


def build(name, specs):
    """specs: "file.wav[:ROOT[:LO-HI]]" -> (header, ADPCM data) for SMP_END / SMP_WRITE

    The path comes first and the numeric fields are anchored to the end (a Windows drive letter's
    ':' is part of the path)."""
    zones = []
    for spec in specs:
        m = re.fullmatch(r"(.+?)(?::(\d*))?(?::(\d+)-(\d+))?", spec)
        if not m:
            raise ValueError(f"{spec!r}: use file.wav[:ROOT[:LO-HI]]")
        path = Path(m.group(1))
        root = int(m.group(2)) if m.group(2) else sio.note_from_name(path.stem)
        lo, hi = (int(m.group(3)), int(m.group(4))) if m.group(3) else (None, None)
        sr, x = sio.read_any_wav(path)
        x = sio.resample(x, sr, sio.SLOT_RATE)
        if not x:
            raise ValueError(f"{path}: no audio")
        zones.append((sio.to_int16(x), root, lo, hi))
    return sio.user_slot(name, zones)


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("info", "load", "erase", "build"):
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd in ("load", "build"):
        if len(sys.argv) < 5:
            sys.exit(__doc__)
        try:
            hdr, data = build(sys.argv[3] if cmd == "load" else sys.argv[2], sys.argv[4:])
        except (OSError, ValueError) as e:
            sys.exit(f"cannot build the slot: {e}")
        if cmd == "build":
            Path(sys.argv[3] + ".hdr").write_bytes(hdr)
            Path(sys.argv[3] + ".bin").write_bytes(data)
            print(f"{sys.argv[2]}: {len(data)} B ADPCM, {hdr[6]} zones")
            return
    if cmd != "info":
        if len(sys.argv) < 3 or sys.argv[2] not in ("1", "2", "3"):
            sys.exit("SLOT is 1, 2 or 3")
        slot = int(sys.argv[2]) - 1
    link = Link()
    if cmd == "info":
        try:
            print("\n".join(slot_info(link.req(15, []))))
        except ValueError as e:
            sys.exit(str(e))
        return
    if cmd == "erase":
        print("erase:", "ok" if link.req(14, [slot], 10)[1] == 0 else "FAILED")
        return
    print(f"USR{slot + 1} {sys.argv[3]}: {len(data)} B ADPCM, {hdr[6]} zones")
    if link.req(11, [slot])[1]:
        sys.exit("begin failed")
    t0 = time.time()
    for off in range(0, len(data), 256):
        a = sio.SLOT_DATA_OFF + off
        r = link.req(12, [slot, a & 0x7F, (a >> 7) & 0x7F, (a >> 14) & 0x7F] + pack7_groups(data[off:off + 256]))
        if r[4]:
            sys.exit(f"write at {a:#x} failed ({r[4]})")
        print(f"\r  {min(off + 256, len(data))} / {len(data)}", end="", flush=True)
    r = link.req(13, [slot] + pack7_groups(hdr), 5)
    print(f"\n  {time.time() - t0:.1f} s, end:", "ok" if r[1] == 0 else f"FAILED ({r[1]}, {RC.get(r[1], '?')})")


if __name__ == "__main__":
    try:
        main()
    except TimeoutError as e:
        sys.exit(f"{e}: is Felucca running, and no other app using its MIDI port?")
