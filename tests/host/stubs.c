/* Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * tests/host: just enough of exec, timer.device, utility and bsdsocket for
 * event_lib.c to run on the host (32-bit, so the Amiga's types keep their
 * sizes). One task; time is a counter (stub_now, microseconds) that a wait
 * moves on to the outstanding timer.device request when nothing else is
 * ready, so the tests never sleep. Sockets are a table of what each
 * descriptor is ready for (stub_ready).
 */
#include "stubs.h"
#include <exec/errors.h>

/* from the C library, declared here so its struct timeval stays out */
void *malloc(__SIZE_TYPE__);
void free(void *);
void *memset(void *, int, __SIZE_TYPE__);
int printf(const char *, ...);
void abort(void);

ev_u64 stub_now;
ULONG stub_sigs;                       /* the task's pending signals */
ULONG stub_ready[STUB_FDS];            /* EVF_* each descriptor is ready for */
LONG stub_errno;
LONG stub_select_fail;                 /* WaitSelect returns -1 with this errno, once */
int stub_waits, stub_selects, stub_polls;
static ULONG allocated_sigs = 0xffff;  /* 0-15 are the system's */
static struct Task the_task;
static struct Library utility_base, timer_base;
static long live_allocs;

/* exec: lists */
void AddTail(struct List *l, struct Node *n)
{
    n->ln_Succ = (struct Node *)&l->lh_Tail;
    n->ln_Pred = l->lh_TailPred;
    l->lh_TailPred->ln_Succ = n;
    l->lh_TailPred = n;
}
void Insert(struct List *l, struct Node *n, struct Node *pred)
{
    if (!pred) pred = (struct Node *)&l->lh_Head;
    n->ln_Succ = pred->ln_Succ;
    n->ln_Pred = pred;
    pred->ln_Succ->ln_Pred = n;
    pred->ln_Succ = n;
}
void Remove(struct Node *n)
{
    n->ln_Pred->ln_Succ = n->ln_Succ;
    n->ln_Succ->ln_Pred = n->ln_Pred;
}
struct Node *RemHead(struct List *l)
{
    struct Node *n = l->lh_Head;
    if (!n->ln_Succ) return NULL;
    Remove(n);
    return n;
}
static void init_list(struct List *l)
{
    l->lh_Head = (struct Node *)&l->lh_Tail;
    l->lh_Tail = NULL;
    l->lh_TailPred = (struct Node *)&l->lh_Head;
}

/* exec: memory */
APTR AllocVec(ULONG size, ULONG req)
{
    void *p = malloc(size);
    if (p && (req & MEMF_CLEAR)) memset(p, 0, size);
    if (p) live_allocs++;
    return p;
}
void FreeVec(APTR p) { if (p) { live_allocs--; free(p); } }
void FreeMem(APTR p, ULONG size) { (void)p; (void)size; }
long stub_live_allocs(void) { return live_allocs; }

/* exec: tasks and signals */
struct Task *FindTask(CONST_STRPTR name) { (void)name; return &the_task; }
BYTE AllocSignal(BYTE n)
{
    int i;
    for (i = 16; i < 32; i++)
        if (!(allocated_sigs & (1UL << i))) { allocated_sigs |= 1UL << i; return (BYTE)i; }
    (void)n;
    return -1;
}
void FreeSignal(BYTE n) { if (n >= 0) allocated_sigs &= ~(1UL << n); }
void Signal(struct Task *t, ULONG sigs) { (void)t; stub_sigs |= sigs; }
ULONG SetSignal(ULONG newsigs, ULONG mask)
{
    ULONG old = stub_sigs;
    stub_sigs = (stub_sigs & ~mask) | (newsigs & mask);
    return old;
}

/* exec: semaphores (one task: nothing to do) */
void InitSemaphore(struct SignalSemaphore *s) { (void)s; }
void ObtainSemaphore(struct SignalSemaphore *s) { (void)s; }
void ReleaseSemaphore(struct SignalSemaphore *s) { (void)s; }

/* exec: ports and messages */
struct MsgPort *CreateMsgPort(void)
{
    struct MsgPort *p = AllocVec(sizeof(*p), MEMF_CLEAR);
    BYTE s;
    if (!p) return NULL;
    s = AllocSignal(-1);
    if (s < 0) { FreeVec(p); return NULL; }
    p->mp_SigBit = s;
    p->mp_SigTask = &the_task;
    init_list(&p->mp_MsgList);
    return p;
}
void DeleteMsgPort(struct MsgPort *p) { if (p) { FreeSignal(p->mp_SigBit); FreeVec(p); } }
void PutMsg(struct MsgPort *p, struct Message *m)
{
    m->mn_Node.ln_Type = NT_MESSAGE;
    AddTail(&p->mp_MsgList, &m->mn_Node);
    stub_sigs |= 1UL << p->mp_SigBit;
}
struct Message *GetMsg(struct MsgPort *p) { return (struct Message *)RemHead(&p->mp_MsgList); }

/* exec: libraries */
struct Library *OpenLibrary(CONST_STRPTR name, ULONG v) { (void)name; (void)v; return &utility_base; }
void CloseLibrary(struct Library *l) { (void)l; }

/* timer.device: one request at a time a loop; it ends when stub_now gets there */
#define MAX_REQ 4
static struct timerequest *reqs[MAX_REQ];
static ev_u64 req_due[MAX_REQ];

APTR CreateIORequest(CONST struct MsgPort *port, ULONG size)
{
    struct IORequest *io = AllocVec(size, MEMF_CLEAR);
    if (io) io->io_Message.mn_ReplyPort = (struct MsgPort *)port;
    return io;
}
void DeleteIORequest(APTR io) { FreeVec(io); }
BYTE OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *io, ULONG flags)
{
    (void)name; (void)unit; (void)flags;
    io->io_Device = (struct Device *)&timer_base;
    io->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    return 0;
}
void CloseDevice(struct IORequest *io) { (void)io; }
static void reply(struct timerequest *t, BYTE err)
{
    t->tr_node.io_Error = err;
    t->tr_node.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    PutMsg(t->tr_node.io_Message.mn_ReplyPort, &t->tr_node.io_Message);
}
void SendIO(struct IORequest *io)
{
    struct timerequest *t = (struct timerequest *)io;
    int i;
    for (i = 0; i < MAX_REQ; i++)
        if (!reqs[i]) {
            reqs[i] = t;
            req_due[i] = stub_now + (ev_u64)t->tr_time.tv_secs * 1000000 + t->tr_time.tv_micro;
            io->io_Message.mn_Node.ln_Type = NT_MESSAGE;
            return;
        }
    printf("stub: too many timer requests\n");
    abort();
}
static int find_req(struct IORequest *io)
{
    int i;
    for (i = 0; i < MAX_REQ; i++) if (reqs[i] == (struct timerequest *)io) return i;
    return -1;
}
struct IORequest *CheckIO(struct IORequest *io) { return find_req(io) < 0 ? io : NULL; }
void AbortIO(struct IORequest *io)
{
    int i = find_req(io);
    if (i < 0) return;
    reqs[i] = NULL;
    reply((struct timerequest *)io, IOERR_ABORTED);
}
BYTE WaitIO(struct IORequest *io)
{
    struct MsgPort *p = io->io_Message.mn_ReplyPort;
    struct Node *n;
    if (find_req(io) >= 0) { printf("stub: WaitIO on a request still out\n"); abort(); }
    for (n = p->mp_MsgList.lh_Head; n->ln_Succ; n = n->ln_Succ)   /* off its reply port */
        if (n == &io->io_Message.mn_Node) { Remove(n); break; }
    return io->io_Error;
}
/* Move time on to the earliest request and end it. FALSE: none is out. */
static BOOL run_clock(void)
{
    int i, best = -1;
    for (i = 0; i < MAX_REQ; i++)
        if (reqs[i] && (best < 0 || req_due[i] < req_due[best])) best = i;
    if (best < 0) return FALSE;
    if (req_due[best] > stub_now) stub_now = req_due[best];
    {
        struct timerequest *t = reqs[best];
        reqs[best] = NULL;
        reply(t, 0);
    }
    return TRUE;
}
ULONG ReadEClock(struct EClockVal *ev)
{
    ev->ev_hi = (ULONG)(stub_now >> 32);
    ev->ev_lo = (ULONG)stub_now;
    return 1000000;                        /* a 1 MHz E-clock: ticks are microseconds */
}

/* utility: hooks called as plain C */
typedef ULONG (*hookfn)(struct Hook *, APTR, APTR);
ULONG CallHookPkt(struct Hook *h, APTR obj, APTR msg) { return ((hookfn)h->h_Entry)(h, obj, msg); }

/* exec: Wait */
ULONG Wait(ULONG mask)
{
    ULONG got;
    stub_waits++;
    while (!(stub_sigs & mask))
        if (!run_clock()) { printf("stub: Wait(0x%lx) would never return\n", (unsigned long)mask); abort(); }
    got = stub_sigs & mask;
    stub_sigs &= ~got;
    return got;
}

/* bsdsocket: WaitSelect as our stack does it (gfopensocket lib_select.c):
 * descriptors ready -> their count, the sets cut down to them; an extra
 * signal -> 0 with *sigs the signals; a zero timeout -> 0 at once with no
 * signals looked at. */
LONG ev_WaitSelect(struct Library *base, LONG nfds, ULONG *r, ULONG *w, ULONG *e, struct timeval *tv, ULONG *sigs)
{
    ULONG mask = sigs ? *sigs : 0;
    (void)base;
    stub_selects++;
    if (stub_select_fail) { stub_errno = stub_select_fail; stub_select_fail = 0; return -1; }
    for (;;) {
        LONG fd, n = 0;
        for (fd = 0; fd < nfds && fd < STUB_FDS; fd++) {
            ULONG bit = 1UL << (fd & 31);
            if ((r && (r[fd >> 5] & bit) && (stub_ready[fd] & EVF_READ)) ||
                (w && (w[fd >> 5] & bit) && (stub_ready[fd] & EVF_WRITE)) ||
                (e && (e[fd >> 5] & bit) && (stub_ready[fd] & EVF_EXCEPT))) n++;
        }
        if (n) {
            for (fd = 0; fd < nfds; fd++) {
                ULONG bit = 1UL << (fd & 31);
                ULONG rd = fd < STUB_FDS ? stub_ready[fd] : 0;
                if (r && !(rd & EVF_READ)) r[fd >> 5] &= ~bit;
                if (w && !(rd & EVF_WRITE)) w[fd >> 5] &= ~bit;
                if (e && !(rd & EVF_EXCEPT)) e[fd >> 5] &= ~bit;
            }
            if (sigs) *sigs = 0;
            return n;
        }
        if (tv && !tv->tv_secs && !tv->tv_micro) { stub_polls++; if (sigs) *sigs = 0; return 0; }
        if (stub_sigs & mask) {
            ULONG got = stub_sigs & mask;
            stub_sigs &= ~got;
            if (sigs) *sigs = got;
            return 0;
        }
        if (!run_clock()) { printf("stub: WaitSelect would never return\n"); abort(); }
    }
}
LONG ev_Errno(struct Library *base) { (void)base; return stub_errno; }
