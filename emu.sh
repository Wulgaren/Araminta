#!/bin/sh
# The local loop on macOS: the open LLVM toolchain (tools/get_open_toolchain.sh), no Docker, then the browser
# emulator on :8790. A build made here is not the package CI ships (BUILDING.md).
cd "$(dirname "$0")" || exit 1
tools/get_open_toolchain.sh
JIELI_DOCKER=0 JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-build/deps/pi32v2-open}" ./build.sh &&
    web/emu/build.sh &&
    python3 -m http.server -d build/emu 8790
