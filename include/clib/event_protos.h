/* Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Made by tools/gen-headers.sh from library/event_lib.sfd: do not edit. */

#ifndef CLIB_EVENT_PROTOS_H
#define CLIB_EVENT_PROTOS_H


#include <exec/types.h>
#include <exec/ports.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <devices/timer.h>
#include <libraries/event.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* "event.library" (GFOpenEvent): one wait for sockets, signals, ports, timers and Ctrl-C */
APTR EV_CreateLoopA(struct TagItem *tags);
APTR EV_CreateLoop(Tag tag1, ...);
VOID EV_DeleteLoop(APTR loop);
APTR EV_AddSocket(APTR loop, LONG fd, ULONG events, struct Hook *hook);
VOID EV_SetSocketEvents(APTR watch, ULONG events);
APTR EV_AddSignals(APTR loop, ULONG mask, struct Hook *hook);
APTR EV_AddPort(APTR loop, struct MsgPort *port, struct Hook *hook);
APTR EV_AddTimer(APTR loop, ULONG secs, ULONG micros, ULONG flags, struct Hook *hook);
VOID EV_SetTimer(APTR watch, ULONG secs, ULONG micros, ULONG flags);
VOID EV_StopTimer(APTR watch);
VOID EV_Remove(APTR watch);
LONG EV_RunOnce(APTR loop, struct timeval *timeout);
LONG EV_Run(APTR loop);
VOID EV_Stop(APTR loop, LONG code);
VOID EV_Wake(APTR loop);
BOOL EV_Post(APTR loop, struct Hook *hook, APTR data);
VOID EV_GetTime(struct timeval *tv);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CLIB_EVENT_PROTOS_H */
