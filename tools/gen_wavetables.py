#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Factory wavetables for the wavetable oscillator: a C header of band-limited mip chains.

Two 16-frame tables. BASIC is a usable morph: sine -> soft triangle -> triangle -> soft saw ->
saw -> bright saw -> square -> 25 / 12.5 / 6.25 / 50 % pulse -> double, hollow, bright variants.
SPECTRAL gives 16 different characters: organ, hollow odd, reed, brass, glass, bell, vowel
formants (A / E / O), metallic highs, bright and dark spectra.

Each frame is an additive spectrum (harmonic, signed amplitude, phase) synthesised at 512,
256, 128, 64, 32, 16 and 8 samples, harmonics limited to size / 2, so no mip level aliases.
The mean is removed; one gain per table (from the peak across the 512-sample frames, 0.90 of
full scale) is used for every frame and every mip level, so morphs do not jump in level.

  tools/gen_wavetables.py [OUT.h]        default: build/gen/felucca_wt.h
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cgen  # noqa: E402

FRAMES = 16
BASE_LOG2 = 9
MIP_LAST_LOG2 = 3
FRAME_SAMPLES = (1 << (BASE_LOG2 + 1)) - (1 << MIP_LAST_LOG2)     # 1016
TARGET = 0.90 * 32767


def harm(ks, amp, phase=0.0):
    """(harmonic, amplitude, phase) rows; amp and phase are constants, sequences or callables of the harmonic"""
    ks = list(ks)
    amps = list(amp) if isinstance(amp, (list, tuple)) else [amp] * len(ks)
    phases = list(phase) if isinstance(phase, (list, tuple)) else [phase] * len(ks)
    return [(k, a(k) if callable(a) else a, p(k) if callable(p) else p) for k, a, p in zip(ks, amps, phases)]


def odd(n):
    return range(1, n + 1, 2)


def saw(n, roll=1.0):
    return [(k, 1.0 / k ** roll, 0.0) for k in range(1, n + 1)]


def triangle(n):
    """Fourier triangle: odd harmonics, 1/k^2, every other one inverted"""
    return [(k, (1.0 if k % 4 == 1 else -1.0) / (k * k), 0.0) for k in odd(n)]


def square(n):
    return [(k, 1.0 / k, 0.0) for k in odd(n)]


def pulse(duty, n):
    """rectangular pulse of the given duty, as a cosine series: 2 sin(pi k d) / (pi k)"""
    out = []
    for k in range(1, n + 1):
        a = 2.0 * math.sin(math.pi * k * duty) / (math.pi * k)
        if abs(a) > 1e-9:
            out.append((k, a, math.pi / 2 - math.pi * k * duty))
    return out


def damp(spec, fc, order=2.0):
    return [(k, a / (1.0 + (k / fc) ** order), ph) for k, a, ph in spec]


def formants(peaks, n=32):
    """1/k source through Gaussian formant peaks: (harmonic centre, width, gain)"""
    return [(k, (1.0 / k) * (0.2 + sum(g * math.exp(-0.5 * ((k - c) / w) ** 2) for c, w, g in peaks)), 0.0)
            for k in range(1, n + 1)]


BASIC = [
    harm([1], 1.0),                                                        # 1 sine
    damp(triangle(31), 14.0),                                              # 2 soft triangle
    triangle(63),                                                          # 3 triangle
    damp(saw(48), 8.0),                                                    # 4 soft saw
    saw(48),                                                               # 5 saw
    saw(128),                                                              # 6 bright saw (full upper octave)
    damp(square(63), 18.0),                                                # 7 soft square
    square(63),                                                            # 8 square
    pulse(0.25, 96),                                                       # 9 pulse 25 %
    pulse(0.125, 160),                                                     # 10 pulse 12.5 %
    pulse(0.0625, 192),                                                    # 11 pulse 6.25 %
    pulse(0.5, 64),                                                        # 12 pulse 50 %
    [(1, 1.0, 0.0), (2, 0.7, 0.0), (4, 0.3, 0.0),                          # 13 double (octave rich)
     (6, 0.12, 0.0), (8, 0.06, 0.0)],
    [(1, 1.0, 0.0), (3, 0.55, 0.0), (5, 0.10, 0.0),                        # 14 hollow (notch at 5)
     (7, 0.35, 0.0), (9, 0.15, 0.0), (11, 0.06, 0.0)],
    [(k, (1.0 / k) * (1.0 + 0.8 * math.exp(-0.5 * ((k - 12) / 4.0) ** 2)), 0.0)  # 15 bright
     for k in range(1, 65)],
    [(k, (1.25 if k % 2 else 0.85) / k, 0.0) for k in range(1, 97)],       # 16 full (odd-leaning)
]

SPECTRAL = [
    harm([1, 2, 3, 4], (1.0, 0.6, 0.4, 0.28)),                             # 1 organ 1,2,3,4
    harm([1, 2, 3, 4, 6, 8], (1.0, 0.6, 0.4, 0.28, 0.16, 0.10)),           # 2 full organ
    harm(odd(15), lambda k: 1.0 / k ** 1.2),                               # 3 hollow odd
    harm(range(1, 25), lambda k: (1.2 if k % 2 else 0.6) / k ** 0.9),      # 4 reed
    harm(range(1, 29), lambda k: (1.4 if 3 <= k <= 6 else 1.0) / k),       # 5 brass
    formants([(6, 1.6, 1.0), (11, 2.0, 0.7), (18, 3.0, 0.25)], 28),        # 6 vowel A
    formants([(4, 1.3, 1.0), (16, 2.5, 0.8), (24, 3.0, 0.3)], 32),         # 7 vowel E
    formants([(3, 1.2, 1.0), (8, 1.8, 0.55), (14, 2.5, 0.2)], 24),         # 8 vowel O
    formants([(5, 1.3, 1.0), (9, 1.6, 0.7), (14, 2.0, 0.3)], 20),          # 9 nasal
    [(1, 0.60, 0.0), (5, 0.50, 0.3), (9, 0.35, 0.7),                       # 10 glass
     (13, 0.25, 1.1), (17, 0.18, 0.2), (23, 0.12, 0.9)],
    [(1, 1.00, 0.0), (2, 0.55, 0.4), (3, 0.35, 0.9), (5, 0.28, 1.4),       # 11 bell
     (7, 0.20, 0.2), (9, 0.15, 2.1), (13, 0.10, 1.0), (17, 0.07, 2.6)],
    [(1, 0.35, 0.0), (6, 0.50, 0.7), (11, 0.35, 1.9), (17, 0.40, 0.3),     # 12 metallic
     (23, 0.25, 2.4), (31, 0.18, 1.2), (41, 0.12, 0.5), (53, 0.08, 2.8)],
    [(1, 0.35, 0.0), (2, 0.15, 0.2), (3, 0.10, 0.5), (7, 0.08, 1.0),       # 13 airy
     (11, 0.12, 0.4), (17, 0.10, 1.7), (23, 0.07, 2.2), (31, 0.05, 0.9),
     (43, 0.04, 2.6)],
    [(k, (1.2 if k % 2 else 0.85) / k, 0.0) for k in range(1, 97)],         # 14 bright
    harm(range(1, 11), lambda k: 1.0 / k ** 1.8),                           # 15 dark
    harm(range(1, 7), lambda k: 1.0 / k ** 1.5),                            # 16 deep
]

TABLES = [("BASIC", BASIC), ("SPECTRAL", SPECTRAL)]


def synth(spec, size):
    """one band-limited cycle from (harmonic, amplitude, phase), mean removed"""
    s = [0.0] * size
    for k, a, ph in spec:
        if k > size // 2:                       # nothing above Nyquist: no mip aliases
            continue
        w = 2.0 * math.pi * k / size
        for i in range(size):
            s[i] += a * math.sin(w * i + ph)
    mean = sum(s) / size
    return [v - mean for v in s]


def build(frames):
    """frame-major mip chain (512, 256, ... 8) and the table's common gain"""
    base = [synth(spec, 1 << BASE_LOG2) for spec in frames]
    peak = max(max(abs(v) for v in s) for s in base)
    gain = TARGET / peak
    data = []
    for spec in frames:
        for lg in range(BASE_LOG2, MIP_LAST_LOG2 - 1, -1):
            for v in synth(spec, 1 << lg):
                v *= gain
                assert -32767.0 <= v <= 32767.0, "a mip level overflows the common gain"
                data.append(round(v))
    return data


def check(name, frames, data):
    assert len(frames) == FRAMES, f"{name}: {len(frames)} frames"
    assert len(data) == FRAMES * FRAME_SAMPLES, f"{name}: {len(data)} samples"
    assert min(data) >= -32767 and max(data) <= 32767, f"{name}: out of range"
    assert max(data) >= int(TARGET) - 1, f"{name}: peak {max(data)}"
    off = 0
    for fi, spec in enumerate(frames):
        assert spec and any(a for _, a, _ in spec), f"{name} frame {fi + 1}: empty spectrum"
        for lg in range(BASE_LOG2, MIP_LAST_LOG2 - 1, -1):
            n = 1 << lg
            blk = data[off:off + n]
            off += n
            assert max(abs(v) for v in blk) >= 1000, f"{name} frame {fi + 1} mip {n}: silent"
            assert abs(sum(blk) / n) <= 1.0, f"{name} frame {fi + 1} mip {n}: DC"
    assert off == len(data)


def header(tables):
    L = ["/* SPDX-License-Identifier: GPL-3.0-only",
         " * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */",
         "/* generated by tools/gen_wavetables.py; do not edit */",
         "#ifndef FELUCCA_WT_H",
         "#define FELUCCA_WT_H",
         "#include <stdint.h>",
         "",
         f"#define WT_NFACTORY {len(tables)}",
         f"#define WT_FRAMES {FRAMES}",
         f"#define WT_BASE_LOG2 {BASE_LOG2}              /* 512 samples */",
         f"#define WT_MIP_LAST_LOG2 {MIP_LAST_LOG2}          /* the chain ends at 8 samples */",
         "#define WT_FRAME_SAMPLES ((1u << (WT_BASE_LOG2 + 1u)) - (1u << WT_MIP_LAST_LOG2))  /* 1016 */",
         '#define WT_NAMES_INIT ' + ", ".join(f'"{n}"' for n, _ in tables),
         "",
         "typedef struct {",
         "    const char *name;",
         "    const int16_t *data;            /* frames x WT_FRAME_SAMPLES, frame-major */",
         "    uint16_t frames;",
         "    uint8_t base_log2;",
         "    uint8_t mip_last_log2;",
         "} wt_table_t;",
         ""]
    for n, d in tables:
        L.append(f"static const int16_t WT_{n}_DATA[WT_FRAMES * WT_FRAME_SAMPLES] = {{")
        L.append(cgen.c_rows(d, 16) + ",")
        L.append("};")
    L.append("static const wt_table_t WT_TABLES[WT_NFACTORY] = {")
    L += [f'    {{"{n}", WT_{n}_DATA, WT_FRAMES, WT_BASE_LOG2, WT_MIP_LAST_LOG2}},' for n, _ in tables]
    L += ["};", "#endif"]
    return "\n".join(L) + "\n"


def main(argv):
    out = Path(argv[1]) if len(argv) > 1 else Path("build/gen/felucca_wt.h")
    tables = []
    for name, frames in TABLES:
        data = build(frames)
        check(name, frames, data)
        tables.append((name, data))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(header(tables))
    print(f"wrote {out}: {len(tables)} tables, {FRAMES} frames, {1 << BASE_LOG2} base, "
          f"{FRAMES * FRAME_SAMPLES * 2} B each")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
