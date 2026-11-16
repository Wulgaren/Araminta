#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Fetch the open pi32v2 LLVM toolchain for local macOS builds (no Docker): clang, ld.lld and the LLVM binary
# tools from jvitkauskas/pi32v2-llvm, laid out with JieLi's names so tools/build.py runs it unchanged.
#   tools/get_open_toolchain.sh [DEST]      (default: build/deps/pi32v2-open)
# Local builds only: this is a different compiler, so the package it makes is not the one CI ships.
# Releases stay on JieLi's toolchain (BUILDING.md).
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
dest="${1:-$root/build/deps/pi32v2-open}"
version=pi32v2-23.1.5-5                  # the pinned release; the helper list in tools/build.py is for it
case "$(uname -s)" in
    Darwin) ;;
    *) echo "get_open_toolchain: macOS only (Linux x86-64 runs JieLi's toolchain natively; see BUILDING.md)" >&2
       exit 1 ;;
esac
case "$(uname -m)" in
    arm64)  host=macos-arm64 ;;
    x86_64) host=macos-x64 ;;
    *) echo "get_open_toolchain: no package for $(uname -m)" >&2; exit 1 ;;
esac
asset="pi32v2-llvm-${version#pi32v2-}-$host.tar.xz"

if [ -f "$dest/OPEN_TOOLCHAIN" ] && [ "$(cat "$dest/OPEN_TOOLCHAIN")" = "$version $host" ]; then
    echo "get_open_toolchain: $dest is $version ($host)"
    echo "JIELI_TOOLCHAIN=$dest"
    exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
base="https://github.com/jvitkauskas/pi32v2-llvm/releases/download/$version"
echo "get_open_toolchain: downloading $asset"
curl -fL --retry 3 -o "$tmp/$asset" "$base/$asset"
curl -fL --retry 3 -o "$tmp/SHA256SUMS" "$base/SHA256SUMS"
want="$(awk -v f="$asset" '$2 == f { print $1 }' "$tmp/SHA256SUMS")"
[ -n "$want" ] || { echo "get_open_toolchain: $asset is not in SHA256SUMS" >&2; exit 1; }
got="$(shasum -a 256 "$tmp/$asset" | cut -d' ' -f1)"
[ "$got" = "$want" ] || { echo "get_open_toolchain: $asset: SHA-256 $got, expected $want" >&2; exit 1; }

rm -rf "$dest"
mkdir -p "$dest/llvm" "$dest/pi32v2/bin" "$dest/common/bin"
tar -xJf "$tmp/$asset" --strip-components=1 -C "$dest/llvm"
rt="$(ls "$dest"/llvm/lib/clang/*/lib/pi32v2/libclang_rt.builtins.a 2>/dev/null | head -1)"
[ -n "$rt" ] || { echo "get_open_toolchain: no compiler-rt builtins in $dest/llvm" >&2; exit 1; }

# tools/build.py calls these four names; the linker gets the builtins the driver would have added
w() {
    { printf '#!/bin/sh\nexec "%s" "$@"' "$2"; [ -n "$3" ] && printf ' "%s"' "$3"; printf '\n'; } > "$1"
    chmod +x "$1"
}
w "$dest/pi32v2/bin/clang" "$dest/llvm/bin/clang" ""
w "$dest/pi32v2/bin/ld" "$dest/llvm/bin/ld.lld" "$rt"
w "$dest/common/bin/objcopy" "$dest/llvm/bin/llvm-objcopy" ""
w "$dest/common/bin/objdump" "$dest/llvm/bin/llvm-objdump" ""
printf '%s %s\n' "$version" "$host" > "$dest/OPEN_TOOLCHAIN"
echo "JIELI_TOOLCHAIN=$dest"
