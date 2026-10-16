#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kuroshita), Hügelton Instruments
"""Check that the stock-FM-1 V15 digest the terminal installer accepts (tools/fm1_install.py
STOCK_V15_SHA256) is the same one the web installer accepts (web/fm1pkg.js STOCK_V15_SHA256).

The two installers are separate implementations of the same "return to the official V15" allowance:
if only one of them is updated the other keeps accepting a file nobody meant to bless (or refuses
the real one). web/ is read-only here.

  tools/check_stock_sha.py            exit 0 when they agree, 1 when they do not
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PY_FILE, JS_FILE = "fm1_install.py", "web/fm1pkg.js"
NAME = "STOCK_V15_SHA256"


def from_py(path):
    text = path.read_text()
    m = re.search(rf'^{NAME}\s*=\s*"([0-9a-f]{{64}})"', text, re.M)
    return m.group(1) if m else None


def from_js(path):
    text = path.read_text()
    m = re.search(rf'export const {NAME}\s*=\s*"([0-9a-f]{{64}})"', text)
    return m.group(1) if m else None


def main():
    py, js = from_py(ROOT / "tools" / PY_FILE), from_js(ROOT / JS_FILE)
    if not py:
        print(f"check_stock_sha: no {NAME} in tools/{PY_FILE}", file=sys.stderr)
        return 1
    if not js:
        print(f"check_stock_sha: no {NAME} in {JS_FILE}", file=sys.stderr)
        return 1
    if py != js:
        print(f"check_stock_sha: the stock V15 digest differs:\n"
              f"  tools/{PY_FILE}  {py}\n"
              f"  {JS_FILE}  {js}\n"
              f"  (both installers must accept the same official V15 file)", file=sys.stderr)
        return 1
    print(f"check_stock_sha: {NAME} agrees in tools/{PY_FILE} and {JS_FILE} ({py})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
