#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Print the Markdown tables every Araminta CPU pull request carries.

  tools/cpu_report.py run --runner build/emu-target/release/emu_test --fwsc build/felucca.fwsc \\
      --elf build/felucca.elf [--scenarios 'idle:2000 busy:6000 dry:3000 keys:9000 churn:6000 stack:6000'] \\
      [--runs 2] [--jobs 4] [--out build/cpu_report] [--footprint]
  tools/cpu_report.py sizes BEFORE_BUILD_DIR AFTER_BUILD_DIR

`run` measures each scenario in its own boot (strict, seed 1, --profile 40), writes the boot's stderr (the
per-core profile) to OUT/<scenario>-<run>.log and prints one table row per run. --footprint runs every
scenario once more with --footprint 40, to a log beside the others; its cold cache distorts load, so it
stays out of the table.

`sizes` compares two build directories (each with felucca.bin and felucca.elf): the flash image, .hot_text,
and the RAMTEXT, RAM and pool headroom build.py keeps (>= 4,096 B RAM, >= 8,192 B pool).
"""
import argparse
import json
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build import ld_regions  # noqa: E402

SCENARIOS = "idle:2000 busy:6000 dry:3000 keys:9000 churn:6000 stack:6000"
SCENARIO_NAMES = ("idle", "busy", "dry", "keys", "churn", "stack")
BUSY_PROJECT = ROOT / "tests" / "emu" / "busy_project.bin"
PROFILE = 40
SPREAD = 0.3                        # A5: runs of one scenario must agree this closely before a delta is trusted
SIZE_ROWS = ("image (flash) B", "`.hot_text` B", "RAMTEXT free B", "RAM free B (≥ 4,096)", "pool free B (≥ 8,192)")


@dataclass(frozen=True)
class Run:
    """one emulator boot: the counters the table's row shows"""
    scenario: str
    index: int
    cpu_pct: float
    cpu1_pct: float
    late: int
    misses_half: float | None


def cases(text):
    """'idle:2000 busy:6000' -> [('idle', 2000), ('busy', 6000)]"""
    out = []
    for case in text.split():
        name, colon, ms = case.partition(":")
        if not colon or name not in SCENARIO_NAMES or not ms.isdigit() or not int(ms):
            raise SystemExit(f"--scenarios {case}: expected name:ms with name in {' '.join(SCENARIO_NAMES)}")
        out.append((name, int(ms)))
    if not out:
        raise SystemExit("--scenarios: no scenario given")
    return out


def emu_command(a, name, ms):
    """one boot of one scenario; the caller appends --profile 40 or --footprint 40"""
    cmd = [str(a.runner), "--fwsc", str(a.fwsc), "--elf", str(a.elf), "--ms", str(ms), "--seed", "1"]
    if name == "busy":                          # the project scenario: a patch PUT before playback
        cmd += ["--scenario", "project", "--project", str(BUSY_PROJECT), "--notes", "4"]
    else:
        cmd += ["--scenario", name]
        if name != "idle":
            cmd += ["--clear"]
    return cmd


def execute(cmd, log):
    """run one boot, keep its stderr (the profile) in log, return the completed process"""
    r = subprocess.run(cmd, capture_output=True, text=True)
    log.write_text(r.stderr)
    if r.returncode:
        tail = "\n".join(r.stderr.splitlines()[-10:])
        raise SystemExit(f"{' '.join(cmd)} exited {r.returncode} (see {log}):\n{tail}")
    return r


def measure(a, name, ms, index, out):
    r = execute([*emu_command(a, name, ms), "--profile", str(PROFILE)], out / f"{name}-{index}.log")
    lines = [ln for ln in r.stdout.splitlines() if ln.strip()]
    if not lines:
        raise SystemExit(f"{name} #{index}: no JSON on stdout (see {out / f'{name}-{index}.log'})")
    row = json.loads(lines[-1])
    misses = row["misses_half"]
    return Run(name, index, float(row["cpu_pct"]), float(row["cpu1_pct"]), int(row["late"]),
               None if misses is None else float(misses))


def footprint(a, name, ms, out):
    """same boot with --footprint 40 instead of --profile 40: a log only, for reading by hand"""
    execute([*emu_command(a, name, ms), "--footprint", str(PROFILE)], out / f"{name}-footprint.log")


def print_table(runs):
    print("| Scenario | core0 % | core1 % | late halves | misses/half |")
    print("|---|---|---|---|---|")
    for r in runs:
        misses = "-" if r.misses_half is None else f"{r.misses_half:.1f}"
        print(f"| {r.scenario} #{r.index} | {r.cpu_pct:.1f} | {r.cpu1_pct:.1f} | {r.late} | {misses} |")
    for name in dict.fromkeys(r.scenario for r in runs):
        values = [r.cpu_pct for r in runs if r.scenario == name]
        spread = max(values) - min(values)
        if spread > SPREAD:
            print(f"spread: {name} core0 differs by {spread:.1f} pts between runs")


def run(args):
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    plan = cases(args.scenarios)
    with ThreadPoolExecutor(args.jobs) as pool:
        measured = [pool.submit(measure, args, name, ms, i, out) for name, ms in plan
                    for i in range(1, args.runs + 1)]
        logged = [pool.submit(footprint, args, name, ms, out) for name, ms in plan] if args.footprint else []
        runs = [f.result() for f in measured]
        for f in logged:
            f.result()
    print_table(runs)


def read_elf(path):
    """(the file's bytes, its ELF32 little-endian sections by name: name -> (type, offset, size, link, entsize))"""
    blob = Path(path).read_bytes()
    if len(blob) < 52 or blob[:4] != b"\x7fELF" or blob[4] != 1 or blob[5] != 1:
        raise SystemExit(f"{path}: not a little-endian ELF32")
    shoff = struct.unpack_from("<I", blob, 0x20)[0]
    shentsize = struct.unpack_from("<H", blob, 0x2E)[0]
    shnum = struct.unpack_from("<H", blob, 0x30)[0]
    shstrndx = struct.unpack_from("<H", blob, 0x32)[0]
    headers = [struct.unpack_from("<IIIIIIIIII", blob, shoff + k * shentsize) for k in range(shnum)]
    str_off, str_size = headers[shstrndx][4], headers[shstrndx][5]
    names = blob[str_off:str_off + str_size]

    def named(off):
        return names[off:names.index(b"\0", off)].decode()

    return blob, {named(h[0]): (h[1], h[4], h[5], h[6], h[9]) for h in headers}


def elf_symbols(blob, sections):
    """name -> value, from the ELF's .symtab"""
    _, sym_off, sym_size, _, sym_entsize = sections[".symtab"]
    _, str_off, str_size, _, _ = sections[".strtab"]
    names = blob[str_off:str_off + str_size]
    out = {}
    for at in range(sym_off, sym_off + sym_size, sym_entsize):
        name, value = struct.unpack_from("<II", blob, at)
        if name:
            out[names[name:names.index(b"\0", name)].decode()] = value
    return out


def build_sizes(build):
    """one build directory's values, in SIZE_ROWS order"""
    blob, sections = read_elf(Path(build) / "felucca.elf")
    syms = elf_symbols(blob, sections)
    regions = ld_regions()
    ram_org, ram_len = regions["RAM"]
    _, pool_len = regions["POOL"]
    ramtext = sum(sections[s][2] for s in (".ram_text", ".c1_text", ".hot_text"))
    return [(Path(build) / "felucca.bin").stat().st_size,
            sections[".hot_text"][2],
            regions["RAMTEXT"][1] - ramtext,
            ram_len - (syms["_bss_end"] - ram_org),
            pool_len - (syms["_pool_end"] - syms["_pool_start"])]


def print_sizes(before, after):
    b, a = build_sizes(before), build_sizes(after)
    print("| Size | before | after | Δ |")
    print("|---|---|---|---|")
    for label, x, y in zip(SIZE_ROWS, b, a):
        print(f"| {label} | {x} | {y} | {y - x} |")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="measure the CPU scenarios and print one row per run")
    r.add_argument("--runner", type=Path, default=Path("build/emu-target/release/emu_test"))
    r.add_argument("--fwsc", type=Path, default=Path("build/felucca.fwsc"))
    r.add_argument("--elf", type=Path, default=Path("build/felucca.elf"))
    r.add_argument("--scenarios", default=SCENARIOS, help="name:ms per scenario (default: %(default)s)")
    r.add_argument("--runs", type=int, default=2, help="boots per scenario (default: %(default)s)")
    r.add_argument("--jobs", type=int, default=4, help="concurrent boots (default: %(default)s)")
    r.add_argument("--out", type=Path, default=Path("build/cpu_report"), help="log directory")
    r.add_argument("--footprint", action="store_true", help="also log --footprint 40 per scenario")
    s = sub.add_parser("sizes", help="print the size table for two build directories")
    s.add_argument("before", type=Path)
    s.add_argument("after", type=Path)
    a = ap.parse_args()
    if a.cmd == "run":
        run(a)
    else:
        print_sizes(a.before, a.after)
    return 0


if __name__ == "__main__":
    sys.exit(main())
