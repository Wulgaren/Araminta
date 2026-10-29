#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Felucca in the browser: the firmware's sources as the host tests build them (web/emu/felucca_web.c), compiled
# to WebAssembly with Emscripten, plus the page. After X0X's web/emu/build.sh (charlesvestal/fm1-x0x, GPL-3.0).
#   web/emu/build.sh   ->  build/emu/{index.html, worklet.js, felucca.wasm, fonts/}
#   build/emu/worklet.js is web/emu/store.js (its "export " stripped) in front of web/emu/worklet.js: the browser
#   loads it with AudioWorklet.addModule, which compiles a classic script, so it may not contain a top-level
#   "export". web/emu/store.js is the module the test (web/emu/emu_test.mjs) imports; edit it there, not here.
# Run after one ./build.sh (build/gen: the tables, the fonts, the CC0 samples).
# -ffp-contract=off: no fused multiply-adds, as the host build. EMU_OPT replaces -O2, EMU_CFLAGS adds flags
# (EMU_CFLAGS=-msimd128: let the compiler vectorise; every browser with AudioWorklet has wasm SIMD).
# The page shows the version (FELUCCA_VERSION) marked "(preview)" before its release; EMU_PREVIEW=0 drops the mark.
# Reruns skip what has not changed: the wasm (firmware sources, build/gen, felucca_web.c, flags, emcc; key in
# build/emu/.wasm-key) and the page files (index.html, store.js, worklet.js, fonts; build/emu/.page-key), so
# editing the page does not recompile the wasm.
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/felucca_tables.h ] || { echo "emu: run ./build.sh once first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten)"; exit 1; }
VERSION=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c)
mkdir -p build/emu/fonts

hash_inputs() { # hash_inputs <meta> <file or dir>...: sha256 of the meta and every file under the paths
    h_meta=$1
    shift
    EMU_META=$h_meta python3 - "$@" <<'EOF'
import hashlib, os, pathlib, sys
h = hashlib.sha256(os.environ["EMU_META"].encode() + b"\0")
for arg in sys.argv[1:]:
    p = pathlib.Path(arg)
    for f in sorted([p] if p.is_file() else [q for q in p.rglob("*") if q.is_file()]):
        h.update(str(f).encode() + b"\0" + f.read_bytes() + b"\0")
print(h.hexdigest())
EOF
}

WASM_KEY=$(hash_inputs "$(emcc --version | head -1)|${EMU_OPT:--O2}|${EMU_CFLAGS:-}|$VERSION" \
    firmware/src firmware/hal build/gen web/emu/felucca_web.c)
PAGE_KEY=$(hash_inputs "$VERSION|${EMU_PREVIEW:-1}" \
    web/emu/index.html web/emu/store.js web/emu/worklet.js web/emu/fonts)

WASM_OK=0
[ -f build/emu/.wasm-key ] && [ "$(cat build/emu/.wasm-key)" = "$WASM_KEY" ] && [ -f build/emu/felucca.wasm ] && WASM_OK=1
PAGE_OK=0
if [ -f build/emu/.page-key ] && [ "$(cat build/emu/.page-key)" = "$PAGE_KEY" ] &&
   [ -f build/emu/index.html ] && [ -f build/emu/worklet.js ] && [ -f build/emu/fonts/DotGothic16-subset.woff ]; then
    PAGE_OK=1
fi
if [ "$WASM_OK" = 1 ] && [ "$PAGE_OK" = 1 ]; then
    echo "emu: build/emu unchanged ($VERSION, $(wc -c < build/emu/felucca.wasm | tr -d ' ') B wasm)"
    exit 0
fi

if [ "$WASM_OK" = 0 ]; then
    # shellcheck disable=SC2086
    emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} \
        "-DFELUCCA_VERSION=\"$VERSION\"" -Ibuild/gen -Ifirmware/src \
        --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
        -o build/emu/felucca.wasm web/emu/felucca_web.c
    echo "$WASM_KEY" > build/emu/.wasm-key
fi

if [ "$PAGE_OK" = 0 ]; then
    [ "${EMU_PREVIEW:-1}" = 0 ] && PREVIEW='' || PREVIEW=' (preview)'
    sed -e "s/@VERSION@/$VERSION/g" -e "s/@PREVIEW@/$PREVIEW/g" web/emu/index.html > build/emu/index.html
    { sed 's/^export //' web/emu/store.js; echo; cat web/emu/worklet.js; } > build/emu/worklet.js
    cp web/emu/fonts/DotGothic16-subset.woff web/emu/fonts/OFL.txt build/emu/fonts/
    echo "$PAGE_KEY" > build/emu/.page-key
fi
echo "emu: build/emu ($(wc -c < build/emu/felucca.wasm | tr -d ' ') B wasm, $VERSION)"