#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build the SAMPLER engine's sample sets from WAV files into a C header.

Samples are stored as IMA ADPCM (4 bit) at their native rate; loop points
and roots come from the WAV 'smpl' chunk and the file name (C3 = MIDI 60
naming), single-cycle waves get their root from rate / loop length.
The ADPCM state at the loop start is stored so loops restart exactly.

Libraries:
  cc0        assets/samples-cc0/ (tools/fetch_cc0.py, Versilian Studios, CC0):
             PIANO, FLUTE, SAX (set 1, once TRANH, and set 4, once PERC, are aliases of PIANO).
             PIANO is the 5-zone piano of 1.0 .. 1.1.5 (1.2 .. 1.4.1 built a reduced 2-zone
             one at 11,025 Hz; 1.5 went back to the full one, the same bytes as 1.1.5)
  CR78       omitted from the ROM bank (Araminta: upload as USR). USR1..3 are indices 5..7.
SAMPLE's factory presets cover every named set (aliases skipped when browsing); set 4
(SMP_PERC_SLOT) still loads as DRUM when selected as a preset.
A retired set keeps its index as an alias: the original's name and zones (no data), and a
preset equal to the original's (SMP_SET_ORIG), so old projects and presets play it and
browsing skips it (params.c enum_orig, ui.c preset_orig).
Set 4 was PERC, the GM-mapped drum kit (Felucca's generated drums and CC0 hand percussion,
~66 KB of flash): retired, the DRUM engine plays the same GM map. A sound that selected it
loads as DRUM with its default kit (core.h drum_from_perc); what that cannot reach (GRAIN's
SRC, a value sent live) plays the alias, PIANO.
(SLICE engine retired: its BREAK / PIANO material is no longer built into this header.)

  gen_samples.py OUT.h                                       the header (tools/build.py)
  gen_samples.py --user-slot PIANO "PIANO HD" build/piano_hd/PIANO_HD
                  a CC0 set as built from its own files (PIANO: the 5 zones of 1.0 .. 1.1.5, 1.5) as a user slot:
                  PREFIX.hdr / .bin (the editor's SMP_END header / SMP_WRITE data), PREFIX.slot (as in flash).
                  Kept for reference and tests only (1.2's PIANO HD file, which old users may hold in USR1..3:
                  it plays as the built-in PIANO does); nothing in the build or the app depends on it.

The header is cached (build/gen_samples.cache) under a hash of every
input file, this script, sampleio.py and the Python version, so unchanged
inputs skip the slow pitch detection.
"""
import hashlib
import math
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402
from sampleio import detect_hz, hz_to_midi, ima_encode, key_split, onset, peak, read_any_wav, resample  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
CACHE = SRC / "build" / "gen_samples.cache"
CC0 = SRC / "assets" / "samples-cc0"
TR = 22050                                   # stored sample rate

# CC0 library: set -> kind ("oneshot" decaying, "sus" looped sustain, "kit" one sample per key)
# Built-in sets (piano, flute, sax); user slots USR1-3 follow (indices 5..7). TRANH was
# removed (66 KB of flash); its index 1 stays, an alias of PIANO ("alias": the set named). PERC
# (the GM kit) was removed (66 KB); its index 4 stays, an alias of PIANO too (PERC_SLOT; its
# sounds load as the DRUM engine). CR78 ROM kit omitted (Araminta: upload as USR).
CC0_SETS = [("PIANO", "oneshot"), ("PIANO", "alias"), ("FLUTE", "sus"), ("SAX", "sus"),
            ("PIANO", "alias")]
PERC_SLOT = 4                                # core.h SMP_SET_PERC: retired PERC alias
MEASURED_TUNING = ()                         # sets whose recordings are not at A440 (was TRANH, ~+35 ct)


KIT_BASE = 53                     # F3, fallback root when a kit file has no note in its name

NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}

ENV = {"wave":(5, 80, 100, 50), "kit": (0, 127, 127, 60), "multi": (0, 85, 0, 75),
       "oneshot": (0, 127, 127, 70), "sus": (12, 80, 120, 60)}

# PIANO's notes cut to 0.75 s (saves flash), faded out over the last SET_FADE s.
SET_KEEP = {"PIANO": 0.75}
SET_FADE = {"PIANO": 0.15}

_wavs = {}


def cos_fade(x, fade):
    """fade the last `fade` samples of x out on a raised cosine (no corner at either end)"""
    n = len(x)
    fade = min(fade, n)
    if fade < 1:
        return
    for i in range(fade):
        x[n - fade + i] *= 0.5 + 0.5 * math.cos(math.pi * (i + 1) / fade)


def wav(path):
    """read_any_wav, once per file and run (the CC0 pass reads every file twice)"""
    if path not in _wavs:
        _wavs[path] = read_any_wav(path)
    return _wavs[path]


def name_note(name):
    """'_C#3' in a CC0 file name -> (octave, pitch class)"""
    m = re.search(r"_([A-G]#?)(-?\d)", name)
    return (int(m.group(2)), NOTE[m.group(1)]) if m else None


def octave_conv(files):
    """octave convention of a set's files (C4 = 60: 1, C3 = 60: 2): YIN votes, the names give the notes"""
    votes = {1: 0, 2: 0}
    for p in files:
        nn = name_note(p.name)
        if not nn:
            continue
        sr, x = wav(p)
        det = hz_to_midi(detect_hz(x[onset(x):], sr))
        for o in (1, 2):
            if abs(det - ((nn[0] + o) * 12 + nn[1])) < 1.0:
                votes[o] += 1
    return 2 if votes[2] >= votes[1] else 1


def kit_root(name, index):
    """MIDI root for a kit zone: note in the file name (C4 = 60), else KIT_BASE + index"""
    nn = name_note(name)
    return (nn[0] + 1) * 12 + nn[1] if nn else KIT_BASE + index


def cc0_entries(setname, kind):
    """-> [(file name, int16 samples at TR, loop or None, root)]"""
    files = sorted((CC0 / setname).glob("*.wav"))
    out = []
    if kind != "kit":
        conv = octave_conv(files)
    for k, p in enumerate(files):
        sr, x = wav(p)
        x = x[max(0, onset(x) - 16):]
        nn = name_note(p.name)
        if kind == "kit":
            root = kit_root(p.name, k)
        elif nn:
            root = (nn[0] + conv) * 12 + nn[1]
            if setname in MEASURED_TUNING:              # tuned off A440: the measured pitch near the named note
                det = hz_to_midi(detect_hz(x, sr))
                if abs(det - root) < 1.0:
                    root = det
        else:
            root = hz_to_midi(detect_hz(x, sr))
        x = resample(x, sr, TR)
        keep = SET_KEEP.get(setname) or {"oneshot": 1.0, "sus": 0.95, "kit": 0.6}[kind]
        x = x[:int(keep * TR)]
        n = len(x)
        if kind == "sus":                            # crossfaded sustain loop in the steady part
            ls, le, xf = int(0.40 * TR), n - 1, int(0.06 * TR)
            for i in range(xf):
                a = i / xf
                x[le - xf + i] = x[le - xf + i] * (1 - a) + x[ls - xf + i] * a
            loop = (ls, le)
        else:
            if setname in SET_FADE:                  # a shortened set: a long raised-cosine fade-out
                cos_fade(x, int(SET_FADE[setname] * TR))
            else:
                fade = int(0.08 * TR)
                for i in range(fade):
                    x[n - fade + i] *= 1 - i / fade
            loop = None
        pk = peak(x)
        out.append((p.name, [int(v * 30000 / pk) for v in x], loop, root))
    return out


class Builder:
    def __init__(self):
        self.zones, self.sets, self.blob, self.kinds = [], [], bytearray(), {}
        self.alias = {}                         # set index -> the set index it aliases

    def add(self, s, loop_start):
        """ADPCM-encode s into the blob (each sample starts on an even offset) -> (offset, state at loop_start)"""
        enc, st = ima_encode(s, loop_start)
        off = len(self.blob)
        self.blob += enc
        if len(self.blob) & 1:
            self.blob.append(0)
        return off, st

    def add_set(self, name, kind, entries):
        """entries: zone dicts with root16 (and key for a kit); assigns the key ranges"""
        if not entries:
            return
        if kind == "kit":
            for e in entries:
                e["lo"] = e["hi"] = e["key"]
        else:
            entries.sort(key=lambda e: e["root16"])
            for e, (lo, hi) in zip(entries, key_split([e["root16"] // 16 for e in entries])):
                e["lo"], e["hi"] = lo, hi
        self.sets.append((name, len(self.zones), len(entries)))
        self.zones += entries
        self.kinds[name] = kind

    def alias_set(self, name):
        """a retired set's index: the zones of the earlier set `name` (no data of its own)"""
        orig = next(i for i, (n, _, _) in enumerate(self.sets) if n == name)
        self.alias[len(self.sets)] = orig
        self.sets.append(self.sets[orig])

    def cc0_set(self, name, kind):
        entries = []
        for k, (_, s, loop, root) in enumerate(cc0_entries(name, kind)):
            ls, le = loop if loop else (len(s), len(s))
            off, st = self.add(s, ls)
            entries.append(dict(off=off, n=len(s), ls=ls, le=le, looped=bool(loop), sr=TR,
                                root16=int(round(root * 16)), pred=st[0], idx=st[1],
                                key=int(round(root)) if kind == "kit" else None))
        self.add_set(name, kind, entries)

    def header(self):
        zones, sets, blob = self.zones, self.sets, self.blob
        L = ["/* generated by tools/gen_samples.py: IMA ADPCM sample sets */", "#pragma once",
             "#include <stdint.h>", ""]
        L.append(f"static const uint8_t SMP_DATA[{max(1, len(blob))}] = {{")
        for i in range(0, len(blob), 32):
            L.append("    " + ",".join(map(str, blob[i:i + 32])) + ",")
        if not blob:
            L.append("    0,")
        L.append("};")
        L.append("static const smp_zone_t SMP_ZONES[] = {")
        for e in zones:
            rate = int(round(e["sr"] / 44100 * 65536))
            L.append(f"    {{{e['off']}, {e['n']}, {e['ls']}, {e['le']}, {rate}, {e['root16']}, {e['pred']}, "
                     f"{e['idx']}, {e['lo']}, {e['hi']}, {1 if e['looped'] else 0}}},")
        if not zones:
            L.append("    {0, 0, 0, 0, 65536, 960, 0, 0, 0, 127, 0},")
        L.append("};")
        L.append("static const smp_set_t SMP_SETS[] = {")
        for name, z0, nz in sets:
            L.append(f'    {{"{name}", {z0}, {nz}}},')
        if not sets:
            L.append('    {"NONE", 0, 1},')
        L.append("};")
        L.append(f"#define SMP_NSETS {max(1, len(sets))}")
        named = sets or [("NONE", 0, 0)]
        # Factory presets: every named set. Aliases (TRANH, PERC) are skipped when browsing; selecting
        # preset PERC_SLOT still loads as DRUM (ui.c). CR78 and later sit after that slot.
        npre = len(named)
        L.append(f"#define SMP_NPRESETS {npre}")
        if len(named) > PERC_SLOT:
            L.append(f"#define SMP_PERC_SLOT {PERC_SLOT}   /* once PERC (the GM kit): an alias, its sounds load as DRUM */")
        L.append("/* aliases among the presets are skipped when browsing (params.c enum_orig / ui.c preset_orig) */")
        L.append("static const preset_t SMP_PRESET_TABLE[] = {")
        for i, (name, _, _) in enumerate(named):
            k = self.kinds.get(name, "wave")
            a, d, s_, r = ENV[k]
            loop = 0 if k == "kit" else 1
            pat = ", PAT(12)" if k == "kit" else ""     # a kit: engines.c PATTERNS[11] BEAT
            si = self.alias.get(i, i)                   # an alias: the original's preset (hidden by its name)
            L.append(f'    {{"{name}", {{{si}, 0, 0, {loop}, 127, 0, 0, 0}}, {{{a}, {d}, {s_}, {r}}}, 0, 0{pat}}},')
        L.append("};")
        orig = [self.alias.get(i, i) for i in range(len(named))]
        L.append("/* SET i plays SMP_SET_ORIG[i]: != i for a retired set kept as an alias (its preset i too) */")
        L.append("static const uint8_t SMP_SET_ORIG[] = {" + ", ".join(map(str, orig)) + "};")
        L.append(f"#define SMP_NALIAS {sum(1 for i, o in enumerate(orig) if o != i and i < npre)}   /* among the presets */")
        names = ", ".join(f'"{n}"' for n, _, _ in named)
        L.append("#define SMP_SET_NAMES_INIT " + names)
        L.append("static const char *const SMP_SET_NAMES[] = {" + names + "};")
        return "\n".join(L) + "\n"

    def summary(self):
        return f"samples: {len(self.sets)} sets, {len(self.zones)} zones, {len(self.blob)} B ADPCM"


def input_key(have_cc0):
    """hash of everything the header depends on"""
    h = hashlib.sha256()
    here = Path(__file__).resolve().parent
    for p in (here / "gen_samples.py", here / "sampleio.py"):
        h.update(p.read_bytes())
    h.update(repr((sys.version_info[:2], have_cc0)).encode())   # sum() differs across versions
    files = sorted(CC0.glob("*/*.wav")) if have_cc0 else []
    for p in files:
        h.update(str(p.relative_to(SRC)).encode() + b"\0")
        h.update(hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()


def main(out):
    have_cc0 = CC0.exists() and any(CC0.glob("*/*.wav"))
    if not have_cc0:
        print("samples: CC0 library not fetched (tools/fetch_cc0.py) - no SAMPLE sets")
    key = input_key(have_cc0)
    try:
        ck, summary, text = CACHE.read_text().split("\n", 2)
        if ck == key:
            Path(out).write_text(text)
            print(summary + " (cached)")
            return
    except (OSError, ValueError):
        pass
    b = Builder()
    if have_cc0:
        for name, kind in CC0_SETS:
            if kind == "alias":
                b.alias_set(name)
            else:
                b.cc0_set(name, kind)
    text = b.header()
    Path(out).write_text(text)
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_text(f"{key}\n{b.summary()}\n{text}")
    print(b.summary())


def user_slot_file(setname, slotname, prefix):
    """a one-shot CC0 set as built from all of its own files (PIANO: the 5 zones of 1.0 .. 1.1.5, 1.5) as a
    user sample slot: PREFIX.hdr (the 480-byte header SMP_END takes), PREFIX.bin (the ADPCM SMP_WRITE takes; as
    tools/fm1_sample_upload.py build writes them) and PREFIX.slot (the slot as stored in flash: header at 0, data at
    512). Each zone's ADPCM bytes, length, rate, root and key range are checked against that set as Builder.cc0_set
    builds it: from USR1..3 it plays the same (one-shot zones there; the slot has no loop)"""
    kind = next((k for n, k in CC0_SETS if n == setname and k != "alias"), None)
    if kind != "oneshot":
        raise SystemExit(f"{setname}: not a one-shot CC0 set")
    ent = cc0_entries(setname, kind)
    hdr, data = sio.user_slot(slotname, [(s, int(round(root)), None, None) for _, s, _, root in ent])
    b = Builder()
    b.cc0_set(setname, kind)
    zl = sorted(b.zones, key=lambda z: z["root16"])
    for k, z in enumerate(zl):
        off, n, ls, le, rate, root16, pred, idx, lo, hi, looped = struct.unpack_from("<5I2h4B", hdr, 32 + 28 * k)
        want = bytes(b.blob[z["off"]:z["off"] + (z["n"] + 1) // 2])
        if (data[off:off + (n + 1) // 2] != want or n != z["n"] or root16 != z["root16"] or (lo, hi) != (z["lo"], z["hi"])
                or rate != int(round(z["sr"] / 44100 * 65536)) or looped or z["looped"]):
            raise SystemExit(f"{setname} zone {k}: the slot differs from the set")
    Path(prefix).parent.mkdir(parents=True, exist_ok=True)
    Path(prefix + ".hdr").write_bytes(hdr)
    Path(prefix + ".bin").write_bytes(data)
    Path(prefix + ".slot").write_bytes(hdr.ljust(sio.SLOT_DATA_OFF, b"\0") + data)
    print(f"{slotname}: {len(zl)} zones, {len(data)} B ADPCM of {sio.SLOT_MAX_DATA} -> {prefix}.hdr / .bin / .slot "
          f"(the zones of {setname} from its own files)")


if __name__ == "__main__":
    if sys.argv[1:2] == ["--user-slot"]:
        if len(sys.argv) != 5:
            sys.exit('gen_samples.py --user-slot SET NAME PREFIX   (e.g. PIANO "PIANO HD" build/piano_hd/PIANO_HD)')
        user_slot_file(*sys.argv[2:5])
    else:
        main(sys.argv[1])
