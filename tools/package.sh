#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The release package: GFOpenEvent-NAME.lha and .zip plus SHA256SUMS.
#   tools/package.sh NAME BUILD_DIR OUT_DIR
#   NAME       e.g. 1.1-rc2 (also the ReadMe's heading)
#   BUILD_DIR  where library/build.sh put event.library and EVTest
# Needs lha (jlha-utils) and zip.
set -eu
NAME=$1; BUILD=$(cd "$2" && pwd); mkdir -p "$3"; OUT=$(cd "$3" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VER=$(sed -n 's/.*\$VER: event.library \([0-9.]*\) .*/\1/p' "$ROOT/library/event_lib.c")
COMMIT=$(git -C "$ROOT" rev-parse --short HEAD)
STAGE=$(mktemp -d); P=$STAGE/GFOpenEvent
mkdir -p "$P/Libs" "$P/C" "$P/Docs" "$P/SDK/SFD" "$P/SDK/FD"
cp "$BUILD/event.library" "$P/Libs/"
cp "$BUILD/EVTest" "$P/C/"
cp -r "$ROOT/include" "$P/SDK/Include"
rm -f "$P/SDK/Include/oa_protos_begin.h" "$P/SDK/Include/oa_protos_end.h"
cp "$ROOT/library/event_lib.sfd" "$P/SDK/SFD/"
cp "$ROOT/library/event_lib.fd" "$P/SDK/FD/"
cp "$ROOT/AutoDocs-GFOpenEvent.md" "$ROOT/README.md" "$ROOT/LICENSE" "$P/Docs/"
cat > "$P/ReadMe.txt" <<TXT
GFOpenEvent $NAME (event.library $VER), $(date -u +'%-d %B %Y')
From DalsinAI/gfopenevent $COMMIT.

event.library: one event loop for Amiga programs. It waits once, with
bsdsocket.library's WaitSelect() or Exec's Wait(), for a program's sockets,
signals, MsgPorts, timers and Ctrl-C, and calls it back for what is ready.

Install:   Copy Libs/event.library LIBS:
Check:     Version LIBS:event.library
Test:      C/EVTest            (or EVTest LIB=Libs/event.library)
           prints PASS/FAIL/SKIP for timers, repeat, signal, port, post,
           udp (needs a TCP/IP stack), idle and break.
SDK:       SDK/Include, SDK/SFD, SDK/FD; Docs/AutoDocs-GFOpenEvent.md

AmigaOS 3.x, 68040 build (soft float; the library itself has none).
Needs utility.library 37 and timer.device.

Designed, tested and delivered by The AmiCradle Team. MIT licence.
TXT
rm -f "$OUT/GFOpenEvent-$NAME.lha" "$OUT/GFOpenEvent-$NAME.zip" "$OUT/SHA256SUMS"
(cd "$STAGE" && lha -aq "$OUT/GFOpenEvent-$NAME.lha" GFOpenEvent >/dev/null && zip -qr "$OUT/GFOpenEvent-$NAME.zip" GFOpenEvent)
(cd "$OUT" && sha256sum "GFOpenEvent-$NAME.lha" "GFOpenEvent-$NAME.zip" > SHA256SUMS)
rm -rf "$STAGE"
ls -l "$OUT"
