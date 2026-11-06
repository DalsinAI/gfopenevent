#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# event.library (bare: no startup code or C library; its RomTag's stub
# comes first) and EVTest, with an os32 stove (bebbo's m68k-amigaos-gcc).
#   library/build.sh [OUT_DIR]     (default build/)
#   STOVE=dir      another os32 stove (default ~/AmiCradle/stoves/os32)
#   CC=path        the compiler itself, in place of the stove's
#   EXTRA_INC=dir  more headers (a toolchain without the NDK: the Open SDK's
#                  OpenNDK/Include_H)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
OUT=${1:-$ROOT/build}
mkdir -p "$OUT"
INC="-I$ROOT/include"
[ -n "${EXTRA_INC:-}" ] && INC="$INC -I$EXTRA_INC"
# -fno-delete-null-pointer-checks: address 0 is real memory on the Amiga.
# Soft float: -m68040 links libnix's soft libm020, not libm881
# (specs/m68k-soft-m68040.specs). The library has no floating point at all;
# EVTest links libnix, so the stove's fpuauto applies to it as to any program.
SOFT="-specs=$ROOT/specs/m68k-soft-m68040.specs"
COMMON="-m68040 $SOFT -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-unused-parameter $INC"
# event_lib.c alone: its start() and RomTag lead the hunk. The library
# carries its own memcpy and memset; -fno-tree-loop-distribute-patterns
# keeps GCC from turning their loops into calls to themselves. -lgcc: the
# 64-bit division of the E-clock time.
"$CC" $COMMON -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -fno-tree-loop-distribute-patterns \
    -nostartfiles -nostdlib \
    -o "$OUT/event.library" "$HERE/event_lib.c" -lgcc
echo "$OUT/event.library ($(wc -c < "$OUT/event.library") bytes)"
"$CC" $COMMON -mcrt=nix20 -Wno-pointer-sign \
    -o "$OUT/EVTest" "$ROOT/tests/amiga/evtest.c"
echo "$OUT/EVTest ($(wc -c < "$OUT/EVTest") bytes)"
