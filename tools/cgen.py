# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kuroshita), Hügelton Instruments
"""C source emission shared by the generators (tools/gen_*.py): the array formatters.
Deliberately dependency-free (unlike aa_raster.py, which needs Pillow), so the table and patch
generators - gen_tables.py, gen_fm6_patches.py - keep running with no third-party packages.
aa_raster.py re-exports c_array for the font and icon rasterisers."""
from typing import Any, Iterable


def c_rows(values: Iterable[Any], per_line: int = 16, fmt: str = "{}", indent: str = "    ", sep: str = ",\n") -> str:
    """values as the rows of a C array body, per_line per row, sep between the rows (no trailing
    separator): for an initializer nested in another one (gen_fm6_patches.py's 128-byte records)"""
    return sep.join(indent + ", ".join(fmt.format(v) for v in values[i:i + per_line])
                    for i in range(0, len(values), per_line))


def c_array(name: str, ctype: str, values: Iterable[Any], per_line: int = 16, fmt: str = "{}") -> str:
    """a `static const ctype name[len(values)]` initializer, per_line values per row, every row
    comma-terminated (the last one too: a further row can always be appended)"""
    lines = [f"static const {ctype} {name}[{len(values)}] = {{"]
    if values:
        lines.append(c_rows(values, per_line, fmt) + ",")
    lines.append("};")
    return "\n".join(lines)
