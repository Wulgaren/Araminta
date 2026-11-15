#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kuroshita), Hügelton Instruments
"""Check that the official-FM-1 digests the terminal installer accepts (tools/fm1_install.py
STOCK_SHA256) are the same ones the web installer accepts (web/fm1pkg.js STOCK).

The two installers are separate implementations of the same "return to the official firmware"
allowance: if only one of them is updated the other keeps accepting a file nobody meant to bless
(or refuses the real one). web/ is read-only here.

  tools/check_stock_sha.py            exit 0 when they agree, 1 when they do not
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PY_FILE, JS_FILE = "fm1_install.py", "web/fm1pkg.js"
DIGEST = re.compile(r"\b[0-9a-f]{64}\b")


def digests(path):
    return set(DIGEST.findall(path.read_text()))


def main():
    py, js = digests(ROOT / "tools" / PY_FILE), digests(ROOT / JS_FILE)
    if not py:
        print(f"check_stock_sha: no SHA-256 digest in tools/{PY_FILE}", file=sys.stderr)
        return 1
    if not js:
        print(f"check_stock_sha: no SHA-256 digest in {JS_FILE}", file=sys.stderr)
        return 1
    if py != js:
        print(f"check_stock_sha: the official firmware digests differ:\n"
              f"  tools/{PY_FILE}  {', '.join(sorted(py)) or '(none)'}\n"
              f"  {JS_FILE}  {', '.join(sorted(js)) or '(none)'}\n"
              f"  (both installers must accept the same official files)", file=sys.stderr)
        return 1
    print(f"check_stock_sha: the official firmware digests agree in tools/{PY_FILE} and {JS_FILE} "
          f"({', '.join(sorted(py))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
