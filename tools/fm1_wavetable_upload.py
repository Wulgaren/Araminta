#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Load wavetables into a Felucca user slot (USR1..USR3) over USB-MIDI.

  fm1_wavetable_upload.py info
  fm1_wavetable_upload.py load SLOT NAME file.wav            (one wavetable file)
  fm1_wavetable_upload.py medley SLOT NAME file.wav ...      (one cycle from each, up to 16)
  fm1_wavetable_upload.py erase SLOT
  fm1_wavetable_upload.py build NAME OUT_PREFIX file.wav     (no device: .hdr/.bin, optional .wav)
  fm1_wavetable_upload.py medley-build NAME OUT_PREFIX files...
  options: --frame N (source frame for a medley, default 0), --pick N (frames kept, default 16),
           --wav PATH (also write a Serum-style 2048/frame .wav)

One Serum .wav already contains a whole table (2048 samples a frame, all frames concatenated); one
slot holds one table: up to 16 frames of 1024 samples with mips. A medley takes one cycle from
each file instead, so a folder of single-cycle waves becomes one morphing table.
Protocol: web/EDITOR_PROTOCOL.md, cmds 11..15 (SMP_BEGIN / WRITE / END). Needs mido.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wavetableio as wt  # noqa: E402
from fm1_sample_upload import Link, pack7_groups, RC, slot_info  # noqa: E402


def build(kind, name, files, frame=0, pick=None):
    if kind == "medley":
        return wt.from_medley([Path(f) for f in files], name or "MEDLEY", frame, pick)
    if len(files) != 1:
        sys.exit("load/build takes one wavetable file; use medley for a folder")
    return wt.from_file(files[0], name or Path(files[0]).stem, pick)


def upload(link, slot, name, hdr, data):
    print(f"USR{slot + 1} {name}: {len(data)} B data, {hdr[6] | hdr[7] << 8} frames")
    if link.req(11, [slot])[1]:
        sys.exit("begin failed")
    for off in range(0, len(data), 256):
        a = wt.WT_SLOT_DATA + off
        r = link.req(12, [slot, a & 0x7F, (a >> 7) & 0x7F, (a >> 14) & 0x7F] + pack7_groups(data[off:off + 256]))
        if r[4]:
            sys.exit(f"write at {a:#x} failed ({r[4]})")
        print(f"\r  {min(off + 256, len(data))} / {len(data)}", end="", flush=True)
    r = link.req(13, [slot] + pack7_groups(hdr), 8)
    print("\n  end:", "ok" if r[1] == 0 else f"FAILED ({r[1]}, {RC.get(r[1], '?')})")


def main(argv):
    argv = list(argv)
    opts = {}
    files = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--frame", "--pos", "--pick", "--wav"):
            if i + 1 >= len(argv):
                sys.exit(f"{a} needs a value")
            opts[a] = argv[i + 1]
            i += 2
        else:
            files.append(a)
            i += 1
    if not files or files[0] not in ("info", "load", "medley", "erase", "build", "medley-build"):
        sys.exit(__doc__)
    cmd = files.pop(0)
    frame = int(opts.get("--frame", opts.get("--pos", 0)))
    pick = int(opts["--pick"]) if "--pick" in opts else None
    if cmd == "info":
        link = Link()
        print("\n".join(slot_info(link.req(15, []))))
        return
    if cmd == "erase":
        if len(files) != 1 or files[0] not in ("1", "2", "3"):
            sys.exit("SLOT is 1, 2 or 3")
        link = Link()
        print("erase:", "ok" if link.req(14, [int(files[0]) - 1], 10)[1] == 0 else "FAILED")
        return
    if cmd in ("build", "medley-build"):
        if len(files) < 3:
            sys.exit(__doc__)
        name, out, src = files[0], files[1], files[2:]
        hdr, data, info = build("medley" if cmd == "medley-build" else "table", name, src, frame, pick)
        Path(out + ".hdr").write_bytes(hdr)
        Path(out + ".bin").write_bytes(data)
        if "--wav" in opts:
            cycles = (wt.medley_frames(src, frame, pick) if cmd == "medley-build" else wt.table_frames(src[0], pick))
            wt.wavetable_wav(cycles, opts["--wav"])
        print(f"{info['name']}: {info['frames']} frames, {len(data)} B data, peak {info['peak']:.4f}")
        return
    if len(files) < 3:
        sys.exit(__doc__)
    kind = "medley" if cmd == "medley" else "table"
    slot_s, name, src = files[0], files[1], files[2:]
    if slot_s not in ("1", "2", "3"):
        sys.exit("SLOT is 1, 2 or 3")
    hdr, data, info = build(kind, name, src, frame, pick)
    if "--wav" in opts:
        cycles = (wt.medley_frames(src, frame, pick) if kind == "medley" else wt.table_frames(src[0], pick))
        wt.wavetable_wav(cycles, opts["--wav"])
    link = Link()
    upload(link, int(slot_s) - 1, name, hdr, data)


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except (OSError, ValueError, TimeoutError) as e:
        sys.exit(str(e))
