/* Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Made by tools/gen-headers.sh from library/event_lib.sfd: do not edit. */

#ifndef _INLINE_EVENT_H
#define _INLINE_EVENT_H

#ifndef _SFDC_VARARG_DEFINED
#define _SFDC_VARARG_DEFINED
#ifdef __HAVE_IPTR_ATTR__
typedef APTR _sfdc_vararg __attribute__((iptr));
#else
typedef ULONG _sfdc_vararg;
#endif /* __HAVE_IPTR_ATTR__ */
#endif /* _SFDC_VARARG_DEFINED */

#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif /* !__INLINE_MACROS_H */

#ifndef EVENT_BASE_NAME
#define EVENT_BASE_NAME EventBase
#endif /* !EVENT_BASE_NAME */

#define EV_CreateLoopA(___tags) \
      LP1(0x1e, APTR, EV_CreateLoopA , struct TagItem *, ___tags, a0,\
      , EVENT_BASE_NAME)

#ifndef NO_INLINE_STDARG
#define EV_CreateLoop(___tag1, ...) \
    ({_sfdc_vararg _tags[] = { ___tag1, __VA_ARGS__ }; EV_CreateLoopA((struct TagItem *) _tags); })
#endif /* !NO_INLINE_STDARG */

#define EV_DeleteLoop(___loop) \
      LP1NR(0x24, EV_DeleteLoop , APTR, ___loop, a0,\
      , EVENT_BASE_NAME)

#define EV_AddSocket(___loop, ___fd, ___events, ___hook) \
      LP4(0x2a, APTR, EV_AddSocket , APTR, ___loop, a0, LONG, ___fd, d0, ULONG, ___events, d1, struct Hook *, ___hook, a1,\
      , EVENT_BASE_NAME)

#define EV_SetSocketEvents(___watch, ___events) \
      LP2NR(0x30, EV_SetSocketEvents , APTR, ___watch, a0, ULONG, ___events, d0,\
      , EVENT_BASE_NAME)

#define EV_AddSignals(___loop, ___mask, ___hook) \
      LP3(0x36, APTR, EV_AddSignals , APTR, ___loop, a0, ULONG, ___mask, d0, struct Hook *, ___hook, a1,\
      , EVENT_BASE_NAME)

#define EV_AddPort(___loop, ___port, ___hook) \
      LP3(0x3c, APTR, EV_AddPort , APTR, ___loop, a0, struct MsgPort *, ___port, a1, struct Hook *, ___hook, a2,\
      , EVENT_BASE_NAME)

#define EV_AddTimer(___loop, ___secs, ___micros, ___flags, ___hook) \
      LP5(0x42, APTR, EV_AddTimer , APTR, ___loop, a0, ULONG, ___secs, d0, ULONG, ___micros, d1, ULONG, ___flags, d2, struct Hook *, ___hook, a1,\
      , EVENT_BASE_NAME)

#define EV_SetTimer(___watch, ___secs, ___micros, ___flags) \
      LP4NR(0x48, EV_SetTimer , APTR, ___watch, a0, ULONG, ___secs, d0, ULONG, ___micros, d1, ULONG, ___flags, d2,\
      , EVENT_BASE_NAME)

#define EV_StopTimer(___watch) \
      LP1NR(0x4e, EV_StopTimer , APTR, ___watch, a0,\
      , EVENT_BASE_NAME)

#define EV_Remove(___watch) \
      LP1NR(0x54, EV_Remove , APTR, ___watch, a0,\
      , EVENT_BASE_NAME)

#define EV_RunOnce(___loop, ___timeout) \
      LP2(0x5a, LONG, EV_RunOnce , APTR, ___loop, a0, struct timeval *, ___timeout, a1,\
      , EVENT_BASE_NAME)

#define EV_Run(___loop) \
      LP1(0x60, LONG, EV_Run , APTR, ___loop, a0,\
      , EVENT_BASE_NAME)

#define EV_Stop(___loop, ___code) \
      LP2NR(0x66, EV_Stop , APTR, ___loop, a0, LONG, ___code, d0,\
      , EVENT_BASE_NAME)

#define EV_Wake(___loop) \
      LP1NR(0x6c, EV_Wake , APTR, ___loop, a0,\
      , EVENT_BASE_NAME)

#define EV_Post(___loop, ___hook, ___data) \
      LP3(0x72, BOOL, EV_Post , APTR, ___loop, a0, struct Hook *, ___hook, a1, APTR, ___data, a2,\
      , EVENT_BASE_NAME)

#define EV_GetTime(___tv) \
      LP1NR(0x78, EV_GetTime , struct timeval *, ___tv, a0,\
      , EVENT_BASE_NAME)

#endif /* !_INLINE_EVENT_H */
