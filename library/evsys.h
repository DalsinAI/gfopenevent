/* Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * evsys.h: the system calls event_lib.c makes. On the Amiga, the NDK's
 * inline calls; with EV_HOST_TEST (tests/host), plain C functions that
 * tests/host/stubs.c gives.
 *
 * bsdsocket.library is called through the program's own base (a socket
 * belongs to the base that made it), never a global one.
 */
#ifndef EVENT_EVSYS_H
#define EVENT_EVSYS_H

#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>

typedef unsigned long long ev_u64;

#ifndef EV_HOST_TEST

#define REG(r, decl) register decl __asm(#r)   /* bebbo gcc: an argument in a register */

#include "../include/oa_protos_begin.h"   /* the protos' unused varargs wrappers dropped */
#include <proto/exec.h>
#include <proto/timer.h>
#include <proto/utility.h>
#include <inline/bsdsocket.h>                /* only its __X_base calls: no global SocketBase */
#include "../include/oa_protos_end.h"

/* bsdsocket.library's two calls, through a base given each time */
#define ev_WaitSelect(base, n, r, w, e, tv, sigs) __WaitSelect_base((base), (n), (r), (w), (e), (tv), (sigs))
#define ev_Errno(base) __Errno_base(base)

#else  /* EV_HOST_TEST */

#define REG(r, decl) decl                       /* plain C arguments, in the same order */

#include <clib/exec_protos.h>
#include <clib/timer_protos.h>
#include <clib/utility_protos.h>
LONG ev_WaitSelect(struct Library *base, LONG nfds, ULONG *r, ULONG *w, ULONG *e, struct timeval *tv, ULONG *sigs);
LONG ev_Errno(struct Library *base);

#endif

#endif
