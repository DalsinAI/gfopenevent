#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# include/clib, include/proto, include/inline and library/event_lib.fd from
# library/event_lib.sfd,
# with the stove's sfdc.
#   tools/gen-headers.sh          SFDC=path  another sfdc (default: on PATH,
#                                            else the os32 stove's)
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SFDC=${SFDC:-$(command -v sfdc || echo "$HOME/AmiCradle/stoves/os32/prefix/bin/sfdc")}
SFD=$ROOT/library/event_lib.sfd
"$SFDC" -q --mode=clib -o "$ROOT/include/clib/event_protos.h" "$SFD"
"$SFDC" -q --mode=proto -o "$ROOT/include/proto/event.h" "$SFD"
"$SFDC" -q --target=m68k-amigaos --mode=macros -o "$ROOT/include/inline/event.h" "$SFD"
# The FD too: the SFD has ==varargs, which a plain SFD-to-FD reading would
# count as a function of its own.
"$SFDC" -q --mode=fd -o "$ROOT/library/event_lib.fd" "$SFD"
# Our banner in place of sfdc's, and sfdc's stock copyright block out.
for f in include/clib/event_protos.h include/proto/event.h include/inline/event.h; do
    awk 'NR == 1 { print "/* Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).";
                   print " * SPDX-License-Identifier: MIT";
                   print " * Made by tools/gen-headers.sh from library/event_lib.sfd: do not edit. */"; next }
         /^\/\*$/ { hold = 1; buf = $0; next }
         hold { buf = buf "\n" $0; if ($0 ~ /\*\/$/) { hold = 0; if (buf !~ /Amiga, Inc/) print buf } ; next }
         { print }' "$ROOT/$f" > "$ROOT/$f.tmp" && mv "$ROOT/$f.tmp" "$ROOT/$f"
done
