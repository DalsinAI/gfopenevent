#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The library's loop built for the host (32-bit, so the Amiga's types keep
# their sizes) with tests/host/stubs.c in place of exec, timer.device,
# utility and bsdsocket, and run.
#   tests/host/run.sh       needs gcc with -m32, and the NDK's headers:
#   NDK=dir                 the NDK's include directory (default the os32 stove's)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
NDK=${NDK:-$HOME/AmiCradle/stoves/os32/prefix/m68k-amigaos/ndk-include}
OUT=${OUT:-$ROOT/build/host}
mkdir -p "$OUT"
CC="gcc -m32 -std=gnu99 -O1 -g -DEV_HOST_TEST -D__stdargs= -fno-builtin-printf -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type -Wno-pointer-sign -I$ROOT/include -I$NDK"
$CC -c -o "$OUT/stubs.o" "$HERE/stubs.c"
$CC -c -o "$OUT/test_loop.o" "$HERE/test_loop.c"
$CC -o "$OUT/test_loop" "$OUT/test_loop.o" "$OUT/stubs.o"
"$OUT/test_loop"
