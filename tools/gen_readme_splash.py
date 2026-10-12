#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Wulgaren
"""Build docs/araminta.png from the device splash mark + Terminus (AF_M).

Matches firmware/src/ui_draw.c draw_splash(): 240×240 black, rose at y=8,
ARAMINTA in ter-u16n (Terminus). Crops to the mark + name, then nearest-
neighbour scales (integer) to fit a 1200×600 README banner without clipping.

  tools/gen_readme_splash.py [OUT.png]
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BDF = ROOT / "assets/fonts/ter-u16n.bdf"
LOGO_H = ROOT / "build/gen/ui_splash_logo.h"
DEFAULT_OUT = ROOT / "docs/araminta.png"
ASC = 12  # Terminus M face baseline (tools/gen_aa_font.py --preset terminus)


def ensure_logo() -> Path:
    LOGO_H.parent.mkdir(parents=True, exist_ok=True)
    if not LOGO_H.exists():
        subprocess.check_call([sys.executable, str(ROOT / "tools/gen_splash_logo.py"), str(LOGO_H)])
    return LOGO_H


def load_logo(path: Path):
    text = path.read_text(encoding="utf-8")
    w = int(re.search(r"#define SPL_LOGO_W\s+(\d+)", text).group(1))
    h = int(re.search(r"#define SPL_LOGO_H\s+(\d+)", text).group(1))
    stride = int(re.search(r"#define SPL_LOGO_STRIDE\s+(\d+)", text).group(1))
    body = re.search(r"SPL_LOGO\[\d+\]\s*=\s*\{([^}]+)\}", text, re.S).group(1)
    nums = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]
    return w, h, stride, nums


def load_bdf(path: Path):
    text = path.read_text(encoding="latin-1")
    glyphs = {}
    for block in text.split("STARTCHAR")[1:]:
        enc = re.search(r"^ENCODING\s+(\d+)", block, re.M)
        bbx = re.search(r"^BBX\s+(\d+)\s+(\d+)\s+(-?\d+)\s+(-?\d+)", block, re.M)
        dwi = re.search(r"^DWIDTH\s+(\d+)", block, re.M)
        bm = re.search(r"^BITMAP\n(.*?)^ENDCHAR", block, re.M | re.S)
        if not (enc and bbx and dwi and bm):
            continue
        code = int(enc.group(1))
        bw, bh, bx, by = map(int, bbx.groups())
        adv = int(dwi.group(1))
        rows = []
        for line in bm.group(1).splitlines():
            line = line.strip()
            if not line:
                continue
            if len(line) % 2:
                line = "0" + line
            val = int(line, 16)
            nbits = len(line) * 4
            rows.append([(val >> (nbits - 1 - i)) & 1 for i in range(bw)])
        glyphs[code] = {"rows": rows, "bw": bw, "bh": bh, "bx": bx, "by": by, "adv": adv}
    return glyphs


def text_w(glyphs, s: str) -> int:
    return sum(glyphs[ord(c)]["adv"] for c in s)


def blit_text(img: Image.Image, x: int, y_top: int, s: str, glyphs) -> None:
    baseline = y_top + ASC
    px = x
    for ch in s:
        g = glyphs[ord(ch)]
        ox = px + g["bx"]
        oy = baseline - g["by"] - g["bh"]
        for ry, row in enumerate(g["rows"]):
            for rx, bit in enumerate(row):
                if bit:
                    xx, yy = ox + rx, oy + ry
                    if 0 <= xx < img.width and 0 <= yy < img.height:
                        img.putpixel((xx, yy), 255)
        px += g["adv"]


def main() -> None:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUT
    ensure_logo()
    lw, lh, stride, nums = load_logo(LOGO_H)
    glyphs = load_bdf(BDF)

    canvas = Image.new("L", (240, 240), 0)
    x0 = (240 - lw) // 2
    y0 = 8  # SPL_LOGO_Y
    for y in range(lh):
        row = nums[y * stride : (y + 1) * stride]
        for x in range(lw):
            if (row[x >> 3] >> (x & 7)) & 1:
                canvas.putpixel((x0 + x, y0 + y), 255)

    name = "ARAMINTA"
    name_y = y0 + lh + 6  # SPL_NAME_Y
    blit_text(canvas, (240 - text_w(glyphs, name)) // 2, name_y, name, glyphs)

    # Fit mark + name in 1200×600 (integer nearest scale; no centre-crop clip)
    bw, bh = 1200, 600
    box = canvas.getbbox()
    if box is None:
        raise SystemExit("splash canvas empty")
    pad = 4
    left = max(0, box[0] - pad)
    top = max(0, box[1] - pad)
    right = min(canvas.width, box[2] + pad)
    bottom = min(canvas.height, box[3] + pad)
    crop = canvas.crop((left, top, right, bottom))
    cw, ch = crop.size
    margin = 24  # keep breathing room like the previous README banner
    scale = max(1, min((bw - 2 * margin) // cw, (bh - 2 * margin) // ch))
    scaled = crop.resize((cw * scale, ch * scale), Image.NEAREST)
    banner = Image.new("L", (bw, bh), 0)
    banner.paste(scaled, ((bw - scaled.width) // 2, (bh - scaled.height) // 2))
    rgb = Image.merge("RGB", (banner, banner, banner))
    out.parent.mkdir(parents=True, exist_ok=True)
    rgb.save(out)
    print(
        f"readme splash: {out} {rgb.size[0]}x{rgb.size[1]} "
        f"Terminus ARAMINTA + device rose ({scale}×, no clip)"
    )


if __name__ == "__main__":
    main()
