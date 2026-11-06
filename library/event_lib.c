/* Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * event.library 1.0: one event loop for a program (DESIGN.md).
 *
 * A loop keeps four lists of watches (sockets, signals, ports, posts) and
 * one list of armed timers, earliest first. EV_RunOnce() builds one wait
 * from them:
 *   - the read, write and except descriptor sets of the socket watches;
 *   - one signal mask: every signal watch's bits, every port's signal,
 *     the loop's wake signal, its timer.device reply port's signal and,
 *     with EVT_BreakStops, Ctrl-C;
 *   - one timer.device request (UNIT_MICROHZ) for the earliest timer or
 *     the caller's timeout, whichever comes first;
 * waits once, with the program's bsdsocket.library WaitSelect() when the
 * loop has a socket base, else with Exec's Wait(), and then calls the
 * Hooks of what is ready.
 *
 * Hooks may add, change and remove any watch, the one being called too:
 * a watch removed while the loop is calling Hooks is only marked, and is
 * freed once the round is over.
 *
 * Built bare by library/build.sh (no startup code, no C library). This
 * file goes first: its start() and RomTag lead the hunk.
 */
#include "evsys.h"
#include "../include/libraries/event.h"

#define LIB_VERSION  EVENT_VERSION
#define LIB_REVISION EVENT_REVISION

struct ExecBase *SysBase;
struct Library *UtilityBase;
struct Device *TimerBase;

int start(void) { return -1; }      /* run as a program: nothing (stays first in the file) */

static const char lib_name[] = EVENTNAME;
static const char lib_id[] = "event.library 1.0 (6.11.2026) GFOpenEvent, AmiCradle Team\r\n";

/* ---- the library base -------------------------------------------------------- */

struct EventBase {
    struct Library lib;
    BPTR seglist;
    struct timerequest treq;          /* only opens timer.device, for ReadEClock */
    ULONG eclock_hz;
};

/* ---- loops and watches ----------------------------------------------------- */

#define W_SOCKET 1
#define W_SIGNAL 2
#define W_PORT   3
#define W_TIMER  4

#define WF_DEAD  0x01                 /* removed while the loop was calling Hooks */
#define WF_NEW   0x02                 /* added while the loop was calling Hooks: not this round */
#define WF_ARMED 0x04                 /* a timer in the loop's armed list */

struct EVLoop;

struct EVWatch {
    struct MinNode node;              /* in one of the loop's lists */
    struct EVLoop *loop;
    struct Hook *hook;
    UBYTE type;                       /* W_* */
    UBYTE flags;                      /* WF_* */
    UWORD pad;
    union {
        struct { LONG fd; ULONG events; } sock;
        struct { ULONG mask; } sig;
        struct { struct MsgPort *port; } port;
        struct { ev_u64 due; ev_u64 interval; ULONG flags; } timer;  /* microseconds */
    } u;
};

struct EVPost {
    struct MinNode node;
    struct Hook *hook;
    APTR data;
};

struct EVLoop {
    struct Task *task;                /* the owner */
    struct Library *sockbase;         /* the owner's bsdsocket.library, or NULL */
    struct MinList sockets, signals, ports, timers, idle_timers;
    struct MinList posts;             /* under postlock */
    struct SignalSemaphore postlock;
    BYTE wakesig;                     /* -1: none */
    UBYTE break_stops;
    UBYTE stopped;                    /* EV_Stop() was called */
    UBYTE tpending;                   /* treq is out */
    UWORD calling;                    /* > 0 while Hooks are being called */
    UWORD dead;                       /* watches marked WF_DEAD */
    LONG stopcode;
    struct MsgPort *tport;
    struct timerequest *treq;
    ev_u64 tdue;                       /* when the outstanding request ends */
    ULONG *fds;                       /* 3 sets of fdwords longs: read, write, except */
    LONG fdwords;
};

/* ---- the library's functions ------------------------------------------------ */

static struct Library *lib_init(REG(d0, struct EventBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct EventBase *base));
static BPTR lib_close(REG(a6, struct EventBase *base));
static BPTR lib_expunge(REG(a6, struct EventBase *base));
static ULONG lib_null(void);

static APTR EV_CreateLoopA(REG(a0, struct TagItem *tags), REG(a6, struct EventBase *base));
static VOID EV_DeleteLoop(REG(a0, APTR loop), REG(a6, struct EventBase *base));
static APTR EV_AddSocket(REG(a0, APTR loop), REG(d0, LONG fd), REG(d1, ULONG events), REG(a1, struct Hook *hook), REG(a6, struct EventBase *base));
static VOID EV_SetSocketEvents(REG(a0, APTR watch), REG(d0, ULONG events), REG(a6, struct EventBase *base));
static APTR EV_AddSignals(REG(a0, APTR loop), REG(d0, ULONG mask), REG(a1, struct Hook *hook), REG(a6, struct EventBase *base));
static APTR EV_AddPort(REG(a0, APTR loop), REG(a1, struct MsgPort *port), REG(a2, struct Hook *hook), REG(a6, struct EventBase *base));
static APTR EV_AddTimer(REG(a0, APTR loop), REG(d0, ULONG secs), REG(d1, ULONG micros), REG(d2, ULONG flags), REG(a1, struct Hook *hook), REG(a6, struct EventBase *base));
static VOID EV_SetTimer(REG(a0, APTR watch), REG(d0, ULONG secs), REG(d1, ULONG micros), REG(d2, ULONG flags), REG(a6, struct EventBase *base));
static VOID EV_StopTimer(REG(a0, APTR watch), REG(a6, struct EventBase *base));
static VOID EV_Remove(REG(a0, APTR watch), REG(a6, struct EventBase *base));
static LONG EV_RunOnce(REG(a0, APTR loop), REG(a1, struct timeval *timeout), REG(a6, struct EventBase *base));
static LONG EV_Run(REG(a0, APTR loop), REG(a6, struct EventBase *base));
static VOID EV_Stop(REG(a0, APTR loop), REG(d0, LONG code), REG(a6, struct EventBase *base));
static VOID EV_Wake(REG(a0, APTR loop), REG(a6, struct EventBase *base));
static BOOL EV_Post(REG(a0, APTR loop), REG(a1, struct Hook *hook), REG(a2, APTR data), REG(a6, struct EventBase *base));
static VOID EV_GetTime(REG(a0, struct timeval *tv), REG(a6, struct EventBase *base));

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)EV_CreateLoopA, (APTR)EV_DeleteLoop, (APTR)EV_AddSocket, (APTR)EV_SetSocketEvents,
    (APTR)EV_AddSignals, (APTR)EV_AddPort, (APTR)EV_AddTimer, (APTR)EV_SetTimer,
    (APTR)EV_StopTimer, (APTR)EV_Remove, (APTR)EV_RunOnce, (APTR)EV_Run,
    (APTR)EV_Stop, (APTR)EV_Wake, (APTR)EV_Post, (APTR)EV_GetTime, (APTR)-1,
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct EventBase), lib_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

/* ---- small helpers (no C library; GCC may call memcpy and memset itself) ------- */

#ifndef EV_HOST_TEST                    /* tests/host links the C library's own */
void *memcpy(void *to, const void *from, unsigned long n)
{
    UBYTE *d = to; const UBYTE *s = from;
    while (n--) *d++ = *s++;
    return to;
}
void *memset(void *p, int c, unsigned long n)
{
    UBYTE *d = p;
    while (n--) *d++ = (UBYTE)c;
    return p;
}
#endif

static void new_list(struct MinList *l)
{
    l->mlh_Head = (struct MinNode *)&l->mlh_Tail;
    l->mlh_Tail = NULL;
    l->mlh_TailPred = (struct MinNode *)&l->mlh_Head;
}
#define FIRST(l)    ((struct EVWatch *)(l)->mlh_Head)
#define NEXT(w)     ((struct EVWatch *)(w)->node.mln_Succ)
#define AT_END(w)   (!(w)->node.mln_Succ)
#define EMPTY(l)    (!(l)->mlh_Head->mln_Succ)

/* ---- time: microseconds from the E-clock, which never goes back ----------------- */

static ev_u64 now_micros(struct EventBase *base)
{
    struct EClockVal ev;
    ev_u64 t;
    ReadEClock(&ev);
    t = (ev_u64)ev.ev_hi << 32 | ev.ev_lo;
    /* t / hz seconds; split so that t * 1000000 can't overflow 64 bits */
    return (t / base->eclock_hz) * 1000000 + (t % base->eclock_hz) * 1000000 / base->eclock_hz;
}

static ev_u64 tv_micros(ULONG secs, ULONG micros)
{
    return (ev_u64)secs * 1000000 + micros;
}

/* ---- the library itself --------------------------------------------------------- */

static struct Library *lib_init(REG(d0, struct EventBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    struct EClockVal ev;
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)lib_name;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = LIB_VERSION;
    base->lib.lib_Revision = LIB_REVISION;
    base->lib.lib_IdString = (APTR)lib_id;
    UtilityBase = OpenLibrary((STRPTR)"utility.library", 37);
    if (!UtilityBase) goto fail;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&base->treq, 0)) goto fail;
    TimerBase = base->treq.tr_node.io_Device;
    base->eclock_hz = ReadEClock(&ev);
    if (!base->eclock_hz) base->eclock_hz = 709379;   /* never 0: it divides */
    return &base->lib;
fail:
    if (UtilityBase) CloseLibrary(UtilityBase);
    UtilityBase = NULL;
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return NULL;
}

static struct Library *lib_open(REG(a6, struct EventBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR do_expunge(struct EventBase *base)
{
    BPTR seg;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seg = base->seglist;
    Remove(&base->lib.lib_Node);
    CloseDevice((struct IORequest *)&base->treq);
    CloseLibrary(UtilityBase);
    UtilityBase = NULL;
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seg;
}

static BPTR lib_close(REG(a6, struct EventBase *base))
{
    if (--base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return do_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct EventBase *base)) { return do_expunge(base); }
static ULONG lib_null(void) { return 0; }

/* ---- tags --------------------------------------------------------------------- */

static struct TagItem *next_tag(struct TagItem **list)
{
    struct TagItem *t = *list;
    while (t) {
        switch (t->ti_Tag) {
        case TAG_DONE: *list = NULL; return NULL;
        case TAG_IGNORE: t++; break;
        case TAG_MORE: t = (struct TagItem *)t->ti_Data; break;
        case TAG_SKIP: t += t->ti_Data + 1; break;
        default: *list = t + 1; return t;
        }
    }
    *list = NULL;
    return NULL;
}

/* ---- loops ---------------------------------------------------------------------- */

static APTR EV_CreateLoopA(REG(a0, struct TagItem *tags), REG(a6, struct EventBase *base))
{
    struct EVLoop *l = AllocVec(sizeof(*l), MEMF_PUBLIC | MEMF_CLEAR);
    struct TagItem *t, *list = tags;
    if (!l) return NULL;
    l->task = FindTask(NULL);
    l->break_stops = 1;
    while ((t = next_tag(&list)) != NULL) {
        switch (t->ti_Tag) {
        case EVT_SocketBase: l->sockbase = (struct Library *)t->ti_Data; break;
        case EVT_BreakStops: l->break_stops = t->ti_Data ? 1 : 0; break;
        }
    }
    new_list(&l->sockets);
    new_list(&l->signals);
    new_list(&l->ports);
    new_list(&l->timers);
    new_list(&l->idle_timers);
    new_list(&l->posts);
    InitSemaphore(&l->postlock);
    l->wakesig = AllocSignal(-1);
    if (l->wakesig < 0) goto fail;
    l->tport = CreateMsgPort();
    if (!l->tport) goto fail;
    l->treq = (struct timerequest *)CreateIORequest(l->tport, sizeof(struct timerequest));
    if (!l->treq) goto fail;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)l->treq, 0)) {
        DeleteIORequest((struct IORequest *)l->treq);
        l->treq = NULL;
        goto fail;
    }
    return l;
fail:
    if (l->tport) DeleteMsgPort(l->tport);
    if (l->wakesig >= 0) FreeSignal(l->wakesig);
    FreeVec(l);
    return NULL;
}

static void free_list(struct MinList *list)
{
    struct MinNode *n;
    while ((n = (struct MinNode *)RemHead((struct List *)list)) != NULL)
        FreeVec(n);
}

static void cancel_timer_request(struct EVLoop *l)
{
    if (!l->tpending) return;
    if (!CheckIO((struct IORequest *)l->treq))
        AbortIO((struct IORequest *)l->treq);
    WaitIO((struct IORequest *)l->treq);
    SetSignal(0, 1UL << l->tport->mp_SigBit);
    l->tpending = 0;
}

static VOID EV_DeleteLoop(REG(a0, APTR loop), REG(a6, struct EventBase *base))
{
    struct EVLoop *l = loop;
    if (!l) return;
    cancel_timer_request(l);
    CloseDevice((struct IORequest *)l->treq);
    DeleteIORequest((struct IORequest *)l->treq);
    DeleteMsgPort(l->tport);
    free_list(&l->sockets);
    free_list(&l->signals);
    free_list(&l->ports);
    free_list(&l->timers);
    free_list(&l->idle_timers);
    ObtainSemaphore(&l->postlock);
    free_list(&l->posts);
    ReleaseSemaphore(&l->postlock);
    FreeSignal(l->wakesig);
    if (l->fds) FreeVec(l->fds);
    FreeVec(l);
}

/* ---- watches -------------------------------------------------------------------- */

static struct EVWatch *new_watch(struct EVLoop *l, UBYTE type, struct Hook *hook, struct MinList *list)
{
    struct EVWatch *w;
    if (!l || !hook) return NULL;
    w = AllocVec(sizeof(*w), MEMF_PUBLIC | MEMF_CLEAR);
    if (!w) return NULL;
    w->loop = l;
    w->hook = hook;
    w->type = type;
    if (l->calling) w->flags |= WF_NEW;
    AddTail((struct List *)list, (struct Node *)&w->node);
    return w;
}

static APTR EV_AddSocket(REG(a0, APTR loop), REG(d0, LONG fd), REG(d1, ULONG events), REG(a1, struct Hook *hook), REG(a6, struct EventBase *base))
{
    struct EVLoop *l = loop;
    struct EVWatch *w;
    if (!l || !l->sockbase || fd < 0) return NULL;
    w = new_watch(l, W_SOCKET, hook, &l->sockets);
    if (!w) return NULL;
    w->u.sock.fd = fd;
    w->u.sock.events = events & (EVF_READ | EVF_WRITE | EVF_EXCEPT);
    return w;
}

static VOID EV_SetSocketEvents(REG(a0, APTR watch), REG(d0, ULONG events), REG(a6, struct EventBase *base))
{
    struct EVWatch *w = watch;
    if (w && w->type == W_SOCKET)
        w->u.sock.events = events & (EVF_READ | EVF_WRITE | EVF_EXCEPT);
}

static APTR EV_AddSignals(REG(a0, APTR loop), REG(d0, ULONG mask), REG(a1, struct Hook *hook), REG(a6, struct EventBase *base))
{
    struct EVLoop *l = loop;
    struct EVWatch *w;
    if (!mask) return NULL;
    w = new_watch(l, W_SIGNAL, hook, &l->signals);
    if (w) w->u.sig.mask = mask;
    return w;
}

static APTR EV_AddPort(REG(a0, APTR loop), REG(a1, struct MsgPort *port), REG(a2, struct Hook *hook), REG(a6, struct EventBase *base))
{
    struct EVLoop *l = loop;
    struct EVWatch *w;
    if (!port) return NULL;
    w = new_watch(l, W_PORT, hook, &l->ports);
    if (w) w->u.port.port = port;
    return w;
}

/* Put an armed timer in the loop's list, earliest first (after any due at
 * the same time, so equal timers fire in the order they were set). */
static void arm(struct EVWatch *w)
{
    struct EVLoop *l = w->loop;
    struct EVWatch *at;
    for (at = FIRST(&l->timers); !AT_END(at); at = NEXT(at))
        if (at->u.timer.due > w->u.timer.due) break;
    /* insert before at: after at's predecessor */
    Insert((struct List *)&l->timers, (struct Node *)&w->node, (struct Node *)at->node.mln_Pred);
    w->flags |= WF_ARMED;
}

static void disarm(struct EVWatch *w)
{
    Remove((struct Node *)&w->node);
    AddTail((struct List *)&w->loop->idle_timers, (struct Node *)&w->node);
    w->flags &= ~WF_ARMED;
}

static void set_timer(struct EventBase *base, struct EVWatch *w, ULONG secs, ULONG micros, ULONG flags)
{
    Remove((struct Node *)&w->node);              /* from the armed, idle or firing list */
    w->flags &= ~WF_ARMED;
    w->u.timer.interval = tv_micros(secs, micros);
    w->u.timer.flags = flags;
    w->u.timer.due = now_micros(base) + w->u.timer.interval;
    arm(w);
}

static APTR EV_AddTimer(REG(a0, APTR loop), REG(d0, ULONG secs), REG(d1, ULONG micros), REG(d2, ULONG flags), REG(a1, struct Hook *hook), REG(a6, struct EventBase *base))
{
    struct EVLoop *l = loop;
    struct EVWatch *w = new_watch(l, W_TIMER, hook, &l->idle_timers);
    if (w) set_timer(base, w, secs, micros, flags);
    return w;
}

static VOID EV_SetTimer(REG(a0, APTR watch), REG(d0, ULONG secs), REG(d1, ULONG micros), REG(d2, ULONG flags), REG(a6, struct EventBase *base))
{
    struct EVWatch *w = watch;
    if (w && w->type == W_TIMER && !(w->flags & WF_DEAD))
        set_timer(base, w, secs, micros, flags);
}

static VOID EV_StopTimer(REG(a0, APTR watch), REG(a6, struct EventBase *base))
{
    struct EVWatch *w = watch;
    if (w && w->type == W_TIMER && (w->flags & WF_ARMED))
        disarm(w);
}

static VOID EV_Remove(REG(a0, APTR watch), REG(a6, struct EventBase *base))
{
    struct EVWatch *w = watch;
    struct EVLoop *l;
    if (!w || (w->flags & WF_DEAD)) return;
    l = w->loop;
    if (w->type == W_TIMER && (w->flags & WF_ARMED))
        disarm(w);                                  /* never fires again */
    if (l->calling) {                               /* a Hook may be walking this list */
        w->flags |= WF_DEAD;
        l->dead++;
        return;
    }
    Remove((struct Node *)&w->node);
    FreeVec(w);
}

/* Free what was removed while Hooks were being called, and let the watches
 * added meanwhile take part from the next round. */
static void sweep(struct EVLoop *l)
{
    struct MinList *lists[5];
    int i;
    lists[0] = &l->sockets; lists[1] = &l->signals; lists[2] = &l->ports;
    lists[3] = &l->timers; lists[4] = &l->idle_timers;
    for (i = 0; i < 5; i++) {
        struct EVWatch *w = FIRST(lists[i]), *next;
        for (; !AT_END(w); w = next) {
            next = NEXT(w);
            if (w->flags & WF_DEAD) {
                Remove((struct Node *)&w->node);
                FreeVec(w);
            } else
                w->flags &= ~WF_NEW;
        }
    }
    l->dead = 0;
}

/* ---- calling Hooks -------------------------------------------------------------- */

static void call(struct EVLoop *l, struct EVWatch *w, struct Hook *hook, ULONG kind, ULONG events, APTR postdata)
{
    struct EVMessage m;
    m.em_Kind = kind;
    m.em_Events = events;
    m.em_Loop = l;
    m.em_Watch = w;
    m.em_UserData = hook->h_Data;
    m.em_Socket = (w && w->type == W_SOCKET) ? w->u.sock.fd : -1;
    m.em_Port = (w && w->type == W_PORT) ? w->u.port.port : NULL;
    m.em_PostData = postdata;
    CallHookPkt(hook, w, &m);
}

static LONG run_posts(struct EVLoop *l)
{
    LONG n = 0;
    for (;;) {
        struct EVPost *p;
        ObtainSemaphore(&l->postlock);
        p = (struct EVPost *)RemHead((struct List *)&l->posts);
        ReleaseSemaphore(&l->postlock);
        if (!p) break;
        call(l, NULL, p->hook, EVK_POST, 0, p->data);
        FreeVec(p);
        n++;
    }
    return n;
}

static LONG run_timers(struct EventBase *base, struct EVLoop *l)
{
    LONG n = 0;
    ev_u64 now = now_micros(base);
    struct EVWatch *w, *next;
    struct MinList fire;
    /* take every timer that is due now; one a Hook sets again (even for 0)
     * fires in a later round, never twice in this one */
    new_list(&fire);
    for (w = FIRST(&l->timers); !AT_END(w); w = next) {
        next = NEXT(w);
        if (w->u.timer.due > now) break;
        if (w->flags & WF_NEW) continue;
        Remove((struct Node *)&w->node);
        AddTail((struct List *)&fire, (struct Node *)&w->node);   /* still WF_ARMED: a Hook's
                                                     * EV_StopTimer or EV_SetTimer takes it out */
    }
    while ((w = (struct EVWatch *)RemHead((struct List *)&fire)) != NULL) {
        w->flags &= ~WF_ARMED;
        if ((w->u.timer.flags & EVTF_REPEAT) && w->u.timer.interval) {
            /* the next time from the time it was due, so a repeat doesn't drift;
             * a loop that fell behind skips what it missed */
            w->u.timer.due += w->u.timer.interval;
            if (w->u.timer.due <= now) w->u.timer.due = now + w->u.timer.interval;
            arm(w);
        } else
            AddTail((struct List *)&l->idle_timers, (struct Node *)&w->node);
        if (w->flags & WF_DEAD) continue;
        call(l, w, w->hook, EVK_TIMER, 0, NULL);
        n++;
    }
    return n;
}

/* ---- the wait ------------------------------------------------------------------- */

/* Make room for descriptor sets up to fd. */
static BOOL grow_fds(struct EVLoop *l, LONG maxfd)
{
    LONG words = maxfd / 32 + 1;
    ULONG *p;
    if (words <= l->fdwords) return TRUE;
    words = (words + 3) & ~3;
    p = AllocVec(3 * words * sizeof(ULONG), MEMF_PUBLIC);
    if (!p) return FALSE;
    if (l->fds) FreeVec(l->fds);
    l->fds = p;
    l->fdwords = words;
    return TRUE;
}

#define FDBIT(set, fd)     ((set)[(fd) >> 5] & (1UL << ((fd) & 31)))
#define FDSET(set, fd)     ((set)[(fd) >> 5] |= (1UL << ((fd) & 31)))

static LONG EV_RunOnce(REG(a0, APTR loop), REG(a1, struct timeval *timeout), REG(a6, struct EventBase *base))
{
    struct EVLoop *l = loop;
    struct EVWatch *w, *next;
    ULONG mask, sigs, tsig, wsig, bsig;
    ULONG *rd = NULL, *wr = NULL, *ex = NULL;
    LONG maxfd = -1, n = 0, ready = 0, i;
    BOOL poll = FALSE;
    ev_u64 now, due = 0;
    BOOL have_due = FALSE;

    if (!l) return EVERR_TASK;
    if (FindTask(NULL) != l->task) return EVERR_TASK;

    /* what can't wait: posts already queued, messages already on a port */
    ObtainSemaphore(&l->postlock);
    if (!EMPTY(&l->posts)) poll = TRUE;
    ReleaseSemaphore(&l->postlock);
    for (w = FIRST(&l->ports); !AT_END(w); w = NEXT(w))
        if (!(w->flags & WF_DEAD) && !EMPTY((struct MinList *)&w->u.port.port->mp_MsgList)) poll = TRUE;

    /* when to wake for the timers or the caller's timeout */
    now = now_micros(base);
    if (!EMPTY(&l->timers)) { due = FIRST(&l->timers)->u.timer.due; have_due = TRUE; }
    if (timeout) {
        ev_u64 t = now + tv_micros(timeout->tv_secs, timeout->tv_micro);
        if (!have_due || t < due) { due = t; have_due = TRUE; }
    }
    if (have_due && due <= now) poll = TRUE;

    tsig = 1UL << l->tport->mp_SigBit;
    wsig = 1UL << l->wakesig;
    bsig = l->break_stops ? SIGBREAKF_CTRL_C : 0;
    mask = tsig | wsig | bsig;
    for (w = FIRST(&l->signals); !AT_END(w); w = NEXT(w))
        if (!(w->flags & WF_DEAD)) mask |= w->u.sig.mask;
    for (w = FIRST(&l->ports); !AT_END(w); w = NEXT(w))
        if (!(w->flags & WF_DEAD)) mask |= 1UL << w->u.port.port->mp_SigBit;

    /* the timer request: keep one that ends at the right time, else send anew */
    if (!poll && have_due) {
        if (l->tpending && l->tdue != due) cancel_timer_request(l);
        if (!l->tpending) {
            ev_u64 d = due - now;
            l->treq->tr_node.io_Command = TR_ADDREQUEST;
            l->treq->tr_time.tv_secs = (ULONG)(d / 1000000);
            l->treq->tr_time.tv_micro = (ULONG)(d % 1000000);
            SetSignal(0, tsig);
            SendIO((struct IORequest *)l->treq);
            l->tpending = 1;
            l->tdue = due;
        }
    }

    /* the descriptor sets */
    if (l->sockbase) {
        for (w = FIRST(&l->sockets); !AT_END(w); w = NEXT(w))
            if (!(w->flags & WF_DEAD) && w->u.sock.events && w->u.sock.fd > maxfd) maxfd = w->u.sock.fd;
        if (maxfd >= 0) {
            if (!grow_fds(l, maxfd)) return EVERR_NOMEM;
            rd = l->fds; wr = rd + l->fdwords; ex = wr + l->fdwords;
            for (i = 0; i < 3 * l->fdwords; i++) l->fds[i] = 0;
            for (w = FIRST(&l->sockets); !AT_END(w); w = NEXT(w)) {
                LONG fd = w->u.sock.fd;
                if (w->flags & WF_DEAD) continue;
                if (w->u.sock.events & EVF_READ) FDSET(rd, fd);
                if (w->u.sock.events & EVF_WRITE) FDSET(wr, fd);
                if (w->u.sock.events & EVF_EXCEPT) FDSET(ex, fd);
            }
        }
    }

    /* the one wait */
    if (l->sockbase && (maxfd >= 0 || !poll)) {
        struct timeval zero;
        zero.tv_secs = 0; zero.tv_micro = 0;
        sigs = mask;
        ready = ev_WaitSelect(l->sockbase, maxfd + 1, rd, wr, ex, poll ? &zero : NULL, &sigs);
        if (ready < 0) {
            /* EINTR: the stack's own break mask (Ctrl-C by default) took the
             * wait; the stack leaves that signal set for the program. */
            if (ev_Errno(l->sockbase) != 4) { if (!poll) cancel_timer_request(l); return EVERR_SELECT; }
            ready = 0;
        }
        if (poll || ready <= 0)                    /* a poll doesn't look at signals; EINTR left them */
            sigs |= SetSignal(0, mask) & mask;
        sigs &= mask;
    } else if (poll) {
        sigs = SetSignal(0, mask) & mask;
    } else {
        sigs = Wait(mask);
    }

    if (l->tpending && CheckIO((struct IORequest *)l->treq)) {
        WaitIO((struct IORequest *)l->treq);
        l->tpending = 0;
    }

    /* call the Hooks */
    l->calling++;
    if (sigs & bsig) {
        l->stopped = 1;
        l->stopcode = EVSTOP_BREAK;
    }
    n += run_posts(l);
    if (ready > 0) {
        for (w = FIRST(&l->sockets); !AT_END(w); w = next) {
            ULONG got = 0;
            LONG fd = w->u.sock.fd;
            next = NEXT(w);
            if (w->flags & (WF_DEAD | WF_NEW) || fd > maxfd) continue;
            if ((w->u.sock.events & EVF_READ) && FDBIT(rd, fd)) got |= EVF_READ;
            if ((w->u.sock.events & EVF_WRITE) && FDBIT(wr, fd)) got |= EVF_WRITE;
            if ((w->u.sock.events & EVF_EXCEPT) && FDBIT(ex, fd)) got |= EVF_EXCEPT;
            if (got) { call(l, w, w->hook, EVK_SOCKET, got, NULL); n++; }
        }
    }
    for (w = FIRST(&l->signals); !AT_END(w); w = next) {
        next = NEXT(w);
        if (w->flags & (WF_DEAD | WF_NEW)) continue;
        if (sigs & w->u.sig.mask) { call(l, w, w->hook, EVK_SIGNAL, sigs & w->u.sig.mask, NULL); n++; }
    }
    /* a port is looked at whether its signal came or not: its signal may
     * have been taken by someone else's Wait, or shared with another port */
    for (w = FIRST(&l->ports); !AT_END(w); w = next) {
        next = NEXT(w);
        if (w->flags & (WF_DEAD | WF_NEW)) continue;
        if (!EMPTY((struct MinList *)&w->u.port.port->mp_MsgList)) { call(l, w, w->hook, EVK_PORT, 0, NULL); n++; }
    }
    n += run_timers(base, l);
    l->calling--;
    if (!l->calling) sweep(l);
    return n;
}

static LONG EV_Run(REG(a0, APTR loop), REG(a6, struct EventBase *base))
{
    struct EVLoop *l = loop;
    LONG r;
    if (!l || FindTask(NULL) != l->task) return EVERR_TASK;
    while (!l->stopped) {             /* an EV_Stop() before EV_Run() counts */
        r = EV_RunOnce(l, NULL, base);
        if (r < 0) return r;
    }
    l->stopped = 0;
    return l->stopcode;
}

static VOID EV_Stop(REG(a0, APTR loop), REG(d0, LONG code), REG(a6, struct EventBase *base))
{
    struct EVLoop *l = loop;
    if (!l) return;
    l->stopcode = code;
    l->stopped = 1;
    if (FindTask(NULL) != l->task) Signal(l->task, 1UL << l->wakesig);
}

static VOID EV_Wake(REG(a0, APTR loop), REG(a6, struct EventBase *base))
{
    struct EVLoop *l = loop;
    if (l) Signal(l->task, 1UL << l->wakesig);
}

static BOOL EV_Post(REG(a0, APTR loop), REG(a1, struct Hook *hook), REG(a2, APTR data), REG(a6, struct EventBase *base))
{
    struct EVLoop *l = loop;
    struct EVPost *p;
    if (!l || !hook) return FALSE;
    p = AllocVec(sizeof(*p), MEMF_PUBLIC | MEMF_CLEAR);
    if (!p) return FALSE;
    p->hook = hook;
    p->data = data;
    ObtainSemaphore(&l->postlock);
    AddTail((struct List *)&l->posts, (struct Node *)&p->node);
    ReleaseSemaphore(&l->postlock);
    Signal(l->task, 1UL << l->wakesig);
    return TRUE;
}

static VOID EV_GetTime(REG(a0, struct timeval *tv), REG(a6, struct EventBase *base))
{
    ev_u64 t = now_micros(base);
    if (!tv) return;
    tv->tv_secs = (ULONG)(t / 1000000);
    tv->tv_micro = (ULONG)(t % 1000000);
}
