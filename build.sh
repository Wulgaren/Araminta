#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Build Felucca on Windows (Git Bash), Linux or macOS (see BUILDING.md).
#   ./build.sh [--release X.Y]
#   JIELI_TOOLCHAIN  JieLi toolchain root (platform defaults: BUILDING.md)
#   AC79_SDK         JieLi AC79 SDK root (platform defaults: BUILDING.md)
set -e
cd "$(dirname "$0")"
native_windows=0
case "$(uname -s)" in
    MINGW*|MSYS*) [ "${JIELI_DOCKER:-0}" = 1 ] || native_windows=1 ;;
esac
if [ "$native_windows" = 1 ]; then
    export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$PWD/build/deps/jieli-windows/C\$/JL}"
    export AC79_SDK="${AC79_SDK:-$PWD/build/deps/ac79}"
    PY="${PYTHON:-python}"
    export PYTHONUTF8=1
    if [ -d build/python ]; then
        export PYTHONPATH="$(cygpath -w "$PWD/build/python")${PYTHONPATH:+;$PYTHONPATH}"
    fi
    compiler="$JIELI_TOOLCHAIN/pi32/bin/clang.exe"
else
    export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
    export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
    PY="${PYTHON:-python3}"
    compiler="$JIELI_TOOLCHAIN/pi32v2/bin/clang"
fi

"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (pip3 install Pillow)"; exit 1; }
[ -x "$compiler" ] || { echo "build.sh: no toolchain in $JIELI_TOOLCHAIN (see BUILDING.md)"; exit 1; }
[ -f "$AC79_SDK/cpu/wl82/tools/uboot.boot" ] || { echo "build.sh: no JieLi AC79 SDK in $AC79_SDK"; exit 1; }
if [ "$native_windows" != 1 ] && { [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; }; then
    docker info >/dev/null 2>&1 || { echo "build.sh: Docker is not running"; exit 1; }
fi
exec "$PY" tools/build.py "$@"
