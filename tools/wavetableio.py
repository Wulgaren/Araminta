# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Wavetable conversion for the WAVETABLE engine's user slots (USR1..USR3).

A Serum wavetable is one WAV file: all frames concatenated, usually 2048 samples each, with a
``clm `` chunk whose first number is the frame size. One file therefore already is a whole table;
the firmware stores 16 frames of 1024 samples plus a mip chain in one 80 KiB sample slot
(eng_sample.c smp_wt_hdr_t, "FWT1"; eng_wt.c wt_read).

Two source modes:
  table(path)          one wavetable file -> up to 16 of its frames (evenly spaced)
  medley(paths)        one cycle from each of up to 16 files -> one table that morphs through them

The conversion is pure stdlib (read_any_wav from sampleio.py, an iterative FFT for band-limited
resampling and mips) so the CLI and tests need no numpy. web/editor.html has a JS port that must
produce the same bytes (web/test_web.mjs checks it).
"""
import math
import struct
import sys
import zlib
from pathlib import Path

import sampleio

WT_MAGIC = 0x31545746                 # "FWT1"
WT_VERSION = 1
WT_FRAMES = 16                       # the editor's default (the format takes any that fit)
WT_BASE_LOG2 = 10                    # 1024 samples a frame
WT_MIP_LAST_LOG2 = 3                 # down to 8 samples
WT_HDR_LEN = 32
WT_SLOT_DATA = 512
WT_SLOT_SIZE = 0x14000
WT_MAX_DATA = WT_SLOT_SIZE - WT_SLOT_DATA
WT_PEAK = 29490                      # 0.90 * 32767, as the factory tables
SERUM_FRAME = 2048


# ------------------------------------------------------------------ FFT ---
def _fft(a, inverse=False):
    """iterative radix-2 FFT in place; len(a) is a power of two (complex values)"""
    n = len(a)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    length = 2
    while length <= n:
        ang = 2.0 * math.pi / length * (1.0 if inverse else -1.0)
        wlen = complex(math.cos(ang), math.sin(ang))
        half = length >> 1
        for i in range(0, n, length):
            w = 1.0 + 0.0j
            for k in range(i, i + half):
                u = a[k]
                v = a[k + half] * w
                a[k] = u + v
                a[k + half] = u - v
                w *= wlen
        length <<= 1
    if inverse:
        for i in range(n):
            a[i] /= n
    return a


def fft_resample(x, n_out):
    """band-limited resampling of one cycle to n_out (a power of two), amplitude preserved"""
    n_in = len(x)
    if n_in == n_out:
        return list(x)
    if n_in <= 0 or n_out <= 0:
        return []
    spec = _fft([complex(v) for v in x])
    out = [0j] * n_out
    scale = n_out / n_in
    keep = min(n_in, n_out) // 2
    out[0] = spec[0] * scale
    for k in range(1, keep):
        out[k] = spec[k] * scale
        out[n_out - k] = spec[n_in - k] * scale
    if n_in < n_out:                  # the old Nyquist becomes a regular harmonic
        out[n_in // 2] = spec[n_in // 2] * scale
        out[n_out - n_in // 2] = spec[n_in - n_in // 2].conjugate() * scale
    elif n_in == n_out:
        out[keep] = complex(spec[keep].real, 0.0) * scale
    return [v.real for v in _fft(out, inverse=True)]


# --------------------------------------------------------------- input ---
def frame_size(path):
    """Serum's clm chunk says the frame length; otherwise 2048 if the data divides, else one frame"""
    d = Path(path).read_bytes()
    try:
        ch = sampleio.wav_chunks(d)
    except ValueError:
        return None
    if b"clm " in ch:
        o, n = ch[b"clm "]
        txt = d[o:o + n].decode("latin1", "replace")
        i = 0
        while i < len(txt) and not txt[i].isdigit():
            i += 1
        if i < len(txt):
            j = i
            while j < len(txt) and txt[j].isdigit():
                j += 1
            v = int(txt[i:j])
            if v > 0:
                return v
    return None


def read_table(path):
    """-> (rate, [float] mono, frame length, frame count)"""
    rate, mono = sampleio.read_any_wav(Path(path))
    fs = frame_size(path)
    if not fs:
        fs = SERUM_FRAME if len(mono) >= SERUM_FRAME and len(mono) % SERUM_FRAME == 0 else len(mono)
    if fs <= 0:
        raise ValueError(f"{path}: empty wav")
    frames = len(mono) // fs
    if frames < 1:
        fs, frames = len(mono), 1
    return rate, mono, fs, frames


def _pick(count, total):
    """count evenly spaced indices into 0..total-1, endpoints included, no duplicates"""
    if count >= total:
        return list(range(total))
    if count <= 1:
        return [0]
    return [int(round(i * (total - 1) / (count - 1))) for i in range(count)]


def _resample_cycle(cycle):
    n = len(cycle)
    if n <= 0:
        return [0.0] * (1 << WT_BASE_LOG2)
    if n & (n - 1) == 0:              # power of two: keep the band limit exact
        return fft_resample(cycle, 1 << WT_BASE_LOG2)
    out = sampleio.resample(cycle, n, 1 << WT_BASE_LOG2)
    if len(out) < (1 << WT_BASE_LOG2):
        out += [0.0] * ((1 << WT_BASE_LOG2) - len(out))
    return out[: 1 << WT_BASE_LOG2]


def table_frames(path, pick=None):
    """one wavetable file -> [(name, [1024 float])] (up to 16 frames)"""
    _rate, mono, fs, count = read_table(path)
    ids = _pick(pick or WT_FRAMES, count)
    return [[mono[j * fs + i] for i in range(fs)] for j in ids]


def medley_frames(paths, pos=0, pick=None):
    """one cycle (frame `pos`) from each file -> up to 16 cycles, for one table"""
    paths = list(paths)
    if pick and pick < len(paths):
        paths = [paths[i] for i in _pick(pick, len(paths))]
    out = []
    for p in paths[:WT_FRAMES]:
        _rate, mono, fs, count = read_table(p)
        j = min(max(0, int(pos)), count - 1)
        out.append([mono[j * fs + i] for i in range(fs)])
    if not out:
        raise ValueError("no input files")
    return out


# ------------------------------------------------------------- build ---
def _dc(x):
    m = sum(x) / len(x) if x else 0.0
    return [v - m for v in x], m


def build_table(cycles, name="USER"):
    """[cycles] (any frame lengths) -> (480-byte FWT1 header, int16 LE data, info dict)"""
    base = 1 << WT_BASE_LOG2
    levels = WT_BASE_LOG2 - WT_MIP_LAST_LOG2 + 1
    norm = []
    for c in cycles:
        x = _resample_cycle(c)
        x, _ = _dc(x)
        norm.append(x)
    if not norm:
        raise ValueError("no frames")
    peak = max((max(abs(v) for v in x) for x in norm), default=0.0)
    if peak <= 0.0:
        raise ValueError("silent wavetable")
    gain = WT_PEAK / peak
    data = bytearray()
    for x in norm:
        spec = _fft([complex(v) for v in x])
        for l in range(levels):
            size = base >> l
            if l == 0:
                y = x
            else:                        # band-limit in the frequency domain, then decimate
                keep = size // 2
                out = [0j] * base
                for k in range(1, keep):
                    out[k] = spec[k]
                    out[base - k] = spec[base - k]
                y = [v.real for v in _fft(out, inverse=True)][:: base // size]
            for v in y:
                s = int(round(v * gain))
                data += struct.pack("<h", max(-32767, min(32767, s)))
    frames = len(norm)
    stride = (base << 1) - (1 << WT_MIP_LAST_LOG2)
    if len(data) != frames * stride * 2:
        raise AssertionError("frame data length")
    if len(data) > WT_MAX_DATA:
        raise ValueError(f"{len(data)} bytes do not fit a slot")
    nm = "".join(ch for ch in name.upper() if 32 <= ord(ch) < 127)[:8] or "USER"
    hdr = struct.pack("<IHHBB2s8sIII", WT_MAGIC, WT_VERSION, frames, WT_BASE_LOG2,
                      WT_MIP_LAST_LOG2, b"\0\0", nm.encode("ascii"), len(data),
                      zlib.crc32(data) & 0xFFFFFFFF, 0)
    return hdr.ljust(480, b"\0"), bytes(data), {"frames": frames, "name": nm, "peak": peak,
                                                "data_len": len(data)}


def from_file(path, name=None, pick=None):
    p = Path(path)
    return build_table(table_frames(p, pick), name or p.stem)


def from_medley(paths, name="MEDLEY", pos=0, pick=None):
    return build_table(medley_frames(paths, pos, pick), name)


# ------------------------------------------------------------ export ---
def wavetable_wav(cycles, path):
    """write the condensed frames as a Serum-style float32 wavetable (2048 a frame, clm 2048)"""
    frames = []
    for c in cycles:
        x = _resample_cycle(c)
        x, _ = _dc(x)
        frames.append(fft_resample(x, SERUM_FRAME))
    peak = max((max(abs(v) for v in f) for f in frames), default=0.0)
    if peak > 0:
        frames = [[v / peak * 0.9 for v in f] for f in frames]
    raw = b"".join(struct.pack("<%df" % len(f), *f) for f in frames)
    clm = b"<!>%d 01000000 wavetable (araminta)\0" % SERUM_FRAME
    fmt = struct.pack("<HHIIHH", 3, 1, 44100, 44100 * 4, 4, 32)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    chunks += b"clm " + struct.pack("<I", len(clm)) + clm + (b"\0" if len(clm) & 1 else b"")
    chunks += b"data" + struct.pack("<I", len(raw)) + raw + (b"\0" if len(raw) & 1 else b"")
    blob = b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks
    Path(path).write_bytes(blob)


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description="convert Serum wavetables to FWT1 slots or .wav")
    ap.add_argument("--name", default=None)
    ap.add_argument("--pick", type=int, default=None, help="frames to keep (default 16)")
    ap.add_argument("--pos", type=int, default=0, help="medley: which frame of each file (default 0)")
    ap.add_argument("--wav", default=None, help="also write a 16-frame Serum-style .wav")
    ap.add_argument("--out", default=None, help="write NAME.hdr / NAME.bin here")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args(argv)
    if len(a.files) > 1:              # a folder selection becomes one medley table
        hdr, data, info = from_medley(a.files, a.name or "MEDLEY", a.pos, a.pick)
        cycles = medley_frames(a.files, a.pos, a.pick)
    else:
        hdr, data, info = from_file(a.files[0], a.name, a.pick)
        cycles = table_frames(a.files[0], a.pick)
    if a.out:
        Path(a.out + ".hdr").write_bytes(hdr)
        Path(a.out + ".bin").write_bytes(data)
    if a.wav:
        wavetable_wav(cycles, a.wav)
    print(f"{info['name']}: {info['frames']} frames, {len(data)} B data, peak {info['peak']:.4f}, "
          f"crc {struct.unpack_from('<I', hdr, 24)[0]:08x}")
    print(f"fits one slot: {len(data) <= WT_MAX_DATA}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
