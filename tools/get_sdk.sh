#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Fetch the three JieLi AC79 SDK files a package needs (Apache-2.0) into DEST.
#   tools/get_sdk.sh [DEST]        (default: build/deps/ac79)
# Only cpu/wl82/tools/{uboot.boot,cfg_tool.bin,cfg/eq_cfg_hw.bin} from the
# AC79NN_SDK_V1.2.1_2023-12-13 tag are kept; the 1 GB checkout is temporary.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
dest="${1:-$root/build/deps/ac79}"
dst="$dest/cpu/wl82/tools"
tag=AC79NN_SDK_V1.2.1_2023-12-13
url=https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

check() {   # check FILE SHA
    actual=$(sha256 "$1")
    [ "$actual" = "$2" ] || { echo "get_sdk: $1: SHA-256 $actual, expected $2" >&2; return 1; }
}

verify() {  # the three files against the SHAs build.py pins
    check "$dst/uboot.boot" 4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4 &&
        check "$dst/cfg_tool.bin" 276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b &&
        check "$dst/cfg/eq_cfg_hw.bin" 41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d
}

if [ -f "$dst/uboot.boot" ] && [ -f "$dst/cfg_tool.bin" ] && [ -f "$dst/cfg/eq_cfg_hw.bin" ] && verify; then
    echo "get_sdk: $dest is in place"
    exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
echo "get_sdk: cloning $tag (about 1 GB, 1-2 min)"
# gitee is flaky from GitHub runners (connection resets); retry with backoff
n=0
until git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$tag" "$url" "$tmp/sdk"; do
    n=$((n + 1))
    [ "$n" -le 5 ] || { echo "get_sdk: clone failed after $n attempts" >&2; exit 1; }
    rm -rf "$tmp/sdk"
    echo "get_sdk: clone attempt $n/5" >&2
    sleep $((n * 15))
done

mkdir -p "$dst/cfg"
cp "$tmp/sdk/cpu/wl82/tools/uboot.boot" "$dst/uboot.boot"
cp "$tmp/sdk/cpu/wl82/tools/cfg_tool.bin" "$dst/cfg_tool.bin"
cp "$tmp/sdk/cpu/wl82/tools/cfg/eq_cfg_hw.bin" "$dst/cfg/eq_cfg_hw.bin"
verify
echo "AC79_SDK=$dest"
