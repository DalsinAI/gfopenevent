/* Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * libraries/event.h: event.library, one event loop for a program.
 *
 * A loop waits once for everything a program waits on: its sockets (with
 * bsdsocket.library's WaitSelect()), any Exec signals, its MsgPorts
 * (Intuition window ports, ARexx, AppMessage, commodities, replies), its
 * timers (one timer.device request a loop) and Ctrl-C. When something is
 * ready it calls the Hook the program gave for it.
 *
 * A loop belongs to the task that made it: only that task adds to it, runs
 * it and deletes it. Other tasks may call EV_Wake(), EV_Stop() and
 * EV_Post() only.
 *
 * Every Hook is called the standard way (utility.library's CallHookPkt):
 * A0 the Hook, A2 the watch (the APTR an EV_Add call gave), A1 a
 * struct EVMessage. The Hook's h_Data is handed back in em_UserData.
 * See AutoDocs-GFOpenEvent.md.
 */
#ifndef LIBRARIES_EVENT_H
#define LIBRARIES_EVENT_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef EXEC_PORTS_H
#include <exec/ports.h>
#endif
#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif
#ifndef UTILITY_HOOKS_H
#include <utility/hooks.h>
#endif

#define EVENTNAME        "event.library"
#define EVENT_VERSION    1
#define EVENT_REVISION   1

/* EV_CreateLoopA() tags */
#define EVT_Dummy        (TAG_USER + 0x45560000)   /* 'EV' */
#define EVT_SocketBase   (EVT_Dummy + 1)  /* struct Library *: the program's own bsdsocket.library
                                           * base (sockets are per base). Without it the loop has
                                           * no sockets and waits with Wait(). */
#define EVT_BreakStops   (EVT_Dummy + 2)  /* BOOL, default TRUE: Ctrl-C stops EV_Run() with
                                           * EVSTOP_BREAK. FALSE: Ctrl-C is the program's, to
                                           * watch with EV_AddSignals() if it wants it. */

/* What a socket watch waits for (EV_AddSocket, EV_SetSocketEvents) and
 * what arrived (em_Events) */
#define EVF_READ         0x0001
#define EVF_WRITE        0x0002
#define EVF_EXCEPT       0x0004

/* EV_AddTimer, EV_SetTimer flags */
#define EVTF_REPEAT      0x0001   /* fire every interval until removed or set again */

/* em_Kind */
#define EVK_SOCKET       1
#define EVK_SIGNAL       2
#define EVK_PORT         3
#define EVK_TIMER        4
#define EVK_POST         5

/* What a Hook gets in A1. Read only; good until the Hook returns. */
struct EVMessage {
    ULONG em_Kind;               /* EVK_* */
    ULONG em_Events;             /* socket: EVF_* that are ready; signal: the bits that
                                  * arrived; port: 0; timer: 0; post: 0 */
    APTR  em_Loop;               /* the loop */
    APTR  em_Watch;              /* the watch (also in A2); NULL for a post */
    APTR  em_UserData;           /* the Hook's h_Data */
    LONG  em_Socket;             /* socket: the descriptor, else -1 */
    struct MsgPort *em_Port;     /* port: the port, else NULL */
    APTR  em_PostData;           /* post: EV_Post()'s data, else NULL */
};

/* EV_Run() results besides the code EV_Stop() was given */
#define EVSTOP_BREAK     (-1L)    /* Ctrl-C (EVT_BreakStops) */
#define EVERR_TASK       (-2L)    /* called from a task that doesn't own the loop */
#define EVERR_SELECT     (-3L)    /* WaitSelect() failed (not EINTR); EV_RunOnce only */
#define EVERR_NOMEM      (-4L)

#endif /* LIBRARIES_EVENT_H */
