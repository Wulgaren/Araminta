#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Download the CC0 samples used by the SAMPLE engine into assets/samples-cc0/.

The files are already in the tree; this re-creates them from the source:
  https://github.com/sgossner/VSCO-2-CE  (CC0 1.0)
  https://github.com/sgossner/VCSL       (CC0 1.0)
Only the first RANGE bytes of each WAV are downloaded.
"""
import functools
import json
import re
import struct
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402

DEST = Path(__file__).resolve().parents[1] / "assets" / "samples-cc0"
RANGE = 420_000
TIMEOUT = 30                              # seconds per request: hung otherwise, with no clue why

VCSL = "VCSL"
VSCO = "VSCO-2-CE"
# set: (repo, directory, [filename regexes, one file per regex, first match wins])
# The sets gen_samples.py builds (CC0_SETS: PIANO, FLUTE, SAX): a set nothing builds is downloaded for
# nothing. Retired sets (TRANH, PERC, CR78, the old kit) are not fetched here.
PICK = {
    "PIANO": (VCSL, "Chordophones/Zithers/Grand Piano, Kawai/Sustains",
              [r"_C1_v2_rr1", r"_C2_v2_rr1", r"_C3_v2_rr1", r"_C4_v2_rr1", r"_C5_v2_rr1"]),
    "FLUTE": (VSCO, "Woodwinds/Flute/susvib", [r"_C4_v1_1", r"_C5_v1_1", r"_C6_v1_1"]),
    "SAX": (VCSL, "Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato",
            [r"_A#1_vl2_rr1", r"_A#2_vl2_rr1", r"_A#3_vl2_rr1"]),
}


@functools.lru_cache(maxsize=None)
def tree(repo):
    """the .wav paths of a repo's master tree (GitHub's API, unauthenticated: 60 calls an hour)"""
    url = f"https://api.github.com/repos/sgossner/{repo}/git/trees/master?recursive=1"
    try:
        with urllib.request.urlopen(url, timeout=TIMEOUT) as r:
            js = json.load(r)
    except urllib.error.HTTPError as e:
        raise SystemExit(f"fetch_cc0: {repo} tree: {e.code} {e.reason} (the unauthenticated API allows "
                         f"60 calls an hour; set a token or retry later)")
    except (urllib.error.URLError, OSError, ValueError) as e:
        raise SystemExit(f"fetch_cc0: {repo} tree: {e}")
    if "tree" not in js:
        raise SystemExit(f"fetch_cc0: {repo} tree: {js.get('message', 'no tree in the reply')}")
    return [e["path"] for e in js["tree"] if e["path"].lower().endswith(".wav")]


def check_wav(url, data):
    """the prefix must be a WAV file whose fmt chunk (and the start of its data) is whole in it"""
    try:
        ch = sio.wav_chunks(data)
        sio.wav_format(data, ch)                  # the fmt chunk must be whole in this prefix
        if b"data" not in ch:
            raise ValueError("no data chunk in these bytes")
    except (ValueError, IndexError) as e:
        raise SystemExit(f"fetch_cc0: {url}: {e}")
    except struct.error as e:
        raise SystemExit(f"fetch_cc0: {url}: the {RANGE} byte prefix ends inside the fmt chunk ({e})")


def fetch(repo, path, dest):
    """the first RANGE bytes of one WAV -> dest (0 when it is already there); the server's 206 and
    the WAV header are checked before anything is written, so a short or error response never
    lands in the library"""
    if dest.exists():
        return 0
    url = f"https://raw.githubusercontent.com/sgossner/{repo}/master/" + urllib.parse.quote(path)
    req = urllib.request.Request(url, headers={"Range": f"bytes=0-{RANGE - 1}"})
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
            status, data = r.status, r.read()
    except urllib.error.HTTPError as e:
        raise SystemExit(f"fetch_cc0: {url}: {e.code} {e.reason}")
    except (urllib.error.URLError, OSError) as e:
        raise SystemExit(f"fetch_cc0: {url}: {e}")
    if status != 206:
        raise SystemExit(f"fetch_cc0: {url}: expected 206 partial content, got {status}")
    if not data:
        raise SystemExit(f"fetch_cc0: {url}: no bytes")
    check_wav(url, data)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    return len(data)


def main():
    fetched = []                       # (set, dest, repo, src)
    for name, (repo, folder, pats) in PICK.items():
        files = tree(repo)
        for k, p in enumerate(pats):
            d, rx = (folder, p) if folder else p
            cand = sorted(f for f in files if f.rsplit("/", 1)[0] == d and re.search(rx, f.rsplit("/", 1)[1]))
            if not cand:
                print(f"  {name}: no match for {rx} in {d}")
                continue
            src = cand[0]
            dest = DEST / name / f"{k:02d}_{src.rsplit('/', 1)[1]}"
            fetch(repo, src, dest)
            fetched.append((name, dest, repo, src))
            print(f"  {name:8s} {dest.name}  {dest.stat().st_size} B")
    if not fetched:
        raise SystemExit("fetch_cc0: nothing matched (a repository moved? a naming changed?)")
    import datetime
    # written from what is on disk, so it cannot claim a file the fetch did not produce
    credits = [f"{name}/{dest.name}  <-  sgossner/{repo}: {src}  ({dest.stat().st_size} bytes)"
               for name, dest, repo, src in fetched]
    (DEST / "ATTRIBUTION.txt").write_text(
        "Felucca SAMPLE engine - source material\n"
        f"Retrieved: {datetime.date.today().isoformat()} (the first {RANGE // 1000} kB of each file, an HTTP "
        "range request; the library is built by tools/gen_samples.py, the fetch only re-creates these files)\n"
        "Licence: CC0 1.0 Universal (public domain dedication)\n"
        "  https://creativecommons.org/publicdomain/zero/1.0/\n"
        "Sources: Versilian Studios (Sam Gossner)\n"
        "  VSCO-2 Community Edition  https://github.com/sgossner/VSCO-2-CE  (repository licence: CC0-1.0)\n"
        "  VCSL                      https://github.com/sgossner/VCSL       (repository licence: CC0-1.0)\n\n"
        + "\n".join(credits) + "\n")
    (DEST / "CREDITS.txt").write_text(
        "Samples from Versilian Studios' VSCO-2 Community Edition and VCSL, both released\n"
        "under CC0 1.0 (public domain). Thanks to Versilian Studios / Sam Gossner.\n"
        f"Only the first {RANGE // 1000} kB of each file is used (see the byte counts below).\n\n"
        + "\n".join(credits) + "\n")
    print(f"fetched {sum(dest.stat().st_size for _, dest, _, _ in fetched) / 1e6:.1f} MB into {DEST}")


if __name__ == "__main__":
    sys.exit(main())
