#!/bin/sh
cd "$(dirname "$0")" || exit 1
colima start && ./build.sh && ./web/emu/build.sh && python3 -m http.server -d build/emu 8790
