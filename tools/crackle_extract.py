#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract 3840-byte FUN* project blobs from felucca-backup / felucca-project JSON
into a directory for tests/punch_crackle_test (CRACKLE_DIR=...). Not committed fixtures."""
import argparse, base64, csv, hashlib, json, re
from pathlib import Path

def safe(s):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", s)[:80]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inputs", nargs="+", help="backup/project JSON files or directories")
    ap.add_argument("-o", "--out", default="build/crackle_projs")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    paths = []
    for inp in args.inputs:
        p = Path(inp)
        if p.is_dir():
            paths += sorted(p.glob("*.json"))
        else:
            paths.append(p)
    seen, rows, fail = set(), [], []
    for src in paths:
        if src.name in seen:
            continue
        seen.add(src.name)
        try:
            d = json.load(open(src))
        except Exception as e:
            fail.append((src.name, str(e))); continue
        items = []
        if d.get("format") == "felucca-project":
            items.append(("project", d.get("slot", 0), d.get("data", "")))
        elif d.get("format") == "felucca-backup":
            for o in d.get("objects", []):
                if o.get("data"):
                    items.append((f"id{o['id']}", o["id"], o["data"]))
        else:
            fail.append((src.name, f"format {d.get('format')}")); continue
        for tag, oid, b64 in items:
            try:
                raw = base64.b64decode(b64)
            except Exception as e:
                fail.append((src.name, f"{tag}: {e}")); continue
            if len(raw) != 3840:
                continue
            name = f"{safe(src.stem)}__{safe(str(tag))}.bin"
            (out / name).write_bytes(raw)
            rows.append(dict(file=src.name, tag=tag, oid=oid, bin=name,
                             magic=raw[:4].decode("latin1"),
                             sha=hashlib.sha256(raw).hexdigest()[:16],
                             firmware=d.get("firmware", "")))
    with open(out / "manifest.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]) if rows else ["file"])
        w.writeheader(); w.writerows(rows)
    print(f"wrote {len(rows)} bins to {out}; {len(fail)} failures")
    for x in fail:
        print(" FAIL", x)

if __name__ == "__main__":
    main()
