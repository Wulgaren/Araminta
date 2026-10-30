#!/bin/sh
colima start && ./build.sh && ./web/emu/build.sh && python3 -m http.server -d build/emu 8790
