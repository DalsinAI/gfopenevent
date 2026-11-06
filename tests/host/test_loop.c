/* Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * tests/host: event.library's loop, run on the host with stubs.c in place
 * of exec, timer.device, utility and bsdsocket. The library file is
 * included whole, so its functions are called as they are.
 */
#include "stubs.h"
#include "../../library/event_lib.c"

int printf(const char *, ...);
void *memset(void *, int, __SIZE_TYPE__);

static struct timeval tvbuf;
static struct timeval *us(ULONG micros) { tvbuf.tv_secs = 0; tvbuf.tv_micro = micros; return &tvbuf; }

static struct EventBase B;
static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static struct Library fake_socketbase;

/* a log of Hook calls */
#define LOGN 64
static struct { ULONG kind, events; APTR watch; APTR user; APTR post; ev_u64 at; } logv[LOGN];
static int nlog;
static ULONG log_hook(struct Hook *h, APTR obj, struct EVMessage *m)
{
    CHECK(m->em_Watch == obj);
    CHECK(m->em_UserData == h->h_Data);
    if (nlog < LOGN) {
        logv[nlog].kind = m->em_Kind; logv[nlog].events = m->em_Events;
        logv[nlog].watch = obj; logv[nlog].user = m->em_UserData;
        logv[nlog].post = m->em_PostData; logv[nlog].at = stub_now;
    }
    nlog++;
    return 0;
}
static void hook_init(struct Hook *h, void *fn, APTR data)
{
    h->h_Entry = (HOOKFUNC)fn;
    h->h_SubEntry = NULL;
    h->h_Data = data;
}

static void setup(void)
{
    memset(&B, 0, sizeof(B));
    B.lib.lib_OpenCnt = 0;
    lib_init(&B, 0, NULL);
    nlog = 0;
    stub_now = 1000;                 /* not 0: catches a missing "now +" */
    stub_sigs = 0;
    memset(stub_ready, 0, sizeof(stub_ready));
    stub_waits = stub_selects = stub_polls = 0;
}

static APTR new_loop(BOOL sockets, BOOL breaks)
{
    struct TagItem tags[3];
    tags[0].ti_Tag = sockets ? EVT_SocketBase : TAG_IGNORE;
    tags[0].ti_Data = (ULONG)&fake_socketbase;
    tags[1].ti_Tag = EVT_BreakStops; tags[1].ti_Data = breaks;
    tags[2].ti_Tag = TAG_DONE;
    return EV_CreateLoopA(tags, &B);
}

static void test_timers_in_order(void)
{
    struct Hook h1, h2, h3;
    APTR l, t1, t2, t3;
    long before = stub_live_allocs();
    int i;
    setup();
    l = new_loop(FALSE, TRUE);
    CHECK(l != NULL);
    hook_init(&h1, log_hook, (APTR)1); hook_init(&h2, log_hook, (APTR)2); hook_init(&h3, log_hook, (APTR)3);
    t3 = EV_AddTimer(l, 0, 30000, 0, &h3, &B);
    t1 = EV_AddTimer(l, 0, 10000, 0, &h1, &B);
    t2 = EV_AddTimer(l, 0, 20000, 0, &h2, &B);
    for (i = 0; i < 10 && nlog < 3; i++) CHECK(EV_RunOnce(l, NULL, &B) >= 0);
    CHECK(nlog == 3);
    CHECK(logv[0].user == (APTR)1 && logv[0].at == 11000 && logv[0].kind == EVK_TIMER);
    CHECK(logv[1].user == (APTR)2 && logv[1].at == 21000);
    CHECK(logv[2].user == (APTR)3 && logv[2].at == 31000);
    CHECK(stub_selects == 0 && stub_waits >= 3);           /* no socket base: Wait() */
    /* fired once only */
    CHECK(EV_RunOnce(l, us(50000), &B) == 0);
    CHECK(nlog == 3);
    CHECK(stub_now == 81000);                               /* the timeout */
    EV_Remove(t1, &B); EV_Remove(t2, &B); EV_Remove(t3, &B);
    EV_DeleteLoop(l, &B);
    CHECK(stub_live_allocs() == before);
}

static APTR rep_watch;
static int rep_count;
static ULONG rep_hook(struct Hook *h, APTR obj, struct EVMessage *m)
{
    log_hook(h, obj, m);
    if (++rep_count == 5) EV_Remove(obj, &B);
    return 0;
}
static void test_repeat_and_remove_self(void)
{
    struct Hook h;
    APTR l;
    long before = stub_live_allocs();
    int i;
    setup();
    l = new_loop(TRUE, TRUE);
    hook_init(&h, rep_hook, NULL);
    rep_count = 0;
    rep_watch = EV_AddTimer(l, 0, 10000, EVTF_REPEAT, &h, &B);
    for (i = 0; i < 20 && rep_count < 5; i++) EV_RunOnce(l, NULL, &B);
    CHECK(rep_count == 5);
    CHECK(logv[4].at == 51000);
    CHECK(logv[0].at == 11000 && logv[1].at == 21000);
    EV_RunOnce(l, us(100000), &B);
    CHECK(rep_count == 5);                                   /* gone */
    CHECK(stub_selects > 0);                                 /* socket base: WaitSelect() */
    EV_DeleteLoop(l, &B);
    CHECK(stub_live_allocs() == before);
}

static void test_signals_ports_posts(void)
{
    struct Hook hs, hp, hpost;
    struct MsgPort *port;
    struct Message msg1, msg2;
    APTR l, ws, wp;
    BYTE sig;
    setup();
    l = new_loop(FALSE, TRUE);
    hook_init(&hs, log_hook, (APTR)10); hook_init(&hp, log_hook, (APTR)11); hook_init(&hpost, log_hook, (APTR)12);
    sig = AllocSignal(-1);
    ws = EV_AddSignals(l, 1UL << sig, &hs, &B);
    port = CreateMsgPort();
    wp = EV_AddPort(l, port, &hp, &B);
    CHECK(ws && wp);
    Signal(NULL, 1UL << sig);
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(nlog == 1 && logv[0].kind == EVK_SIGNAL && logv[0].events == (1UL << sig));
    /* a message: the port's Hook is called while the message is still there */
    PutMsg(port, &msg1);
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(nlog == 2 && logv[1].kind == EVK_PORT);
    /* the program didn't take it: the next round doesn't wait (no timer, no
     * signal: a wait here would never return) */
    stub_sigs = 0;
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(nlog == 3);
    GetMsg(port);
    /* a message whose signal someone else took */
    PutMsg(port, &msg2);
    stub_sigs = 0;
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(nlog == 4);
    GetMsg(port);
    /* a post */
    CHECK(EV_Post(l, &hpost, (APTR)0x1234, &B));
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(nlog == 5 && logv[4].kind == EVK_POST && logv[4].post == (APTR)0x1234 && logv[4].watch == NULL);
    EV_DeleteLoop(l, &B);
    DeleteMsgPort(port);
    FreeSignal(sig);
}

static void test_sockets(void)
{
    struct Hook h5, h9;
    APTR l, w5, w9;
    setup();
    l = new_loop(TRUE, TRUE);
    hook_init(&h5, log_hook, (APTR)5); hook_init(&h9, log_hook, (APTR)9);
    CHECK(EV_AddSocket(new_loop(FALSE, TRUE), 3, EVF_READ, &h5, &B) == NULL);   /* no socket base */
    w5 = EV_AddSocket(l, 5, EVF_READ | EVF_WRITE, &h5, &B);
    w9 = EV_AddSocket(l, 40, EVF_READ, &h9, &B);                               /* past the first long */
    stub_ready[5] = EVF_READ;
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(nlog == 1 && logv[0].kind == EVK_SOCKET && logv[0].events == EVF_READ && logv[0].user == (APTR)5);
    stub_ready[5] = EVF_READ | EVF_WRITE; stub_ready[40] = EVF_READ | EVF_WRITE;
    CHECK(EV_RunOnce(l, NULL, &B) == 2);
    CHECK(logv[1].events == (EVF_READ | EVF_WRITE) && logv[2].events == EVF_READ && logv[2].user == (APTR)9);
    /* interest off: not called even when ready */
    EV_SetSocketEvents(w5, 0, &B);
    EV_SetSocketEvents(w9, 0, &B);
    CHECK(EV_RunOnce(l, us(1000), &B) == 0);
    CHECK(nlog == 3);
    EV_SetSocketEvents(w5, EVF_WRITE, &B);
    CHECK(EV_RunOnce(l, NULL, &B) == 1 && logv[3].events == EVF_WRITE);
    /* a failed WaitSelect */
    stub_select_fail = 22;
    CHECK(EV_RunOnce(l, NULL, &B) == EVERR_SELECT);
    EV_DeleteLoop(l, &B);
}

static APTR victim;
static ULONG killer_hook(struct Hook *h, APTR obj, struct EVMessage *m)
{
    log_hook(h, obj, m);
    if (victim) { EV_Remove(victim, &B); victim = NULL; }
    return 0;
}
static void test_remove_other_while_calling(void)
{
    struct Hook hk, hv;
    APTR l;
    BYTE sig;
    long before = stub_live_allocs();
    setup();
    l = new_loop(FALSE, TRUE);
    hook_init(&hk, killer_hook, (APTR)1); hook_init(&hv, log_hook, (APTR)2);
    sig = AllocSignal(-1);
    EV_AddSignals(l, 1UL << sig, &hk, &B);
    victim = EV_AddSignals(l, 1UL << sig, &hv, &B);
    Signal(NULL, 1UL << sig);
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(nlog == 1 && logv[0].user == (APTR)1);
    /* also a timer removed by an earlier Hook in the same round */
    {
        struct Hook ht;
        APTR t;
        hook_init(&ht, log_hook, (APTR)3);
        t = EV_AddTimer(l, 0, 0, 0, &ht, &B);
        victim = t;
        Signal(NULL, 1UL << sig);
        nlog = 0;
        EV_RunOnce(l, NULL, &B);
        CHECK(nlog == 1 && logv[0].user == (APTR)1);
    }
    EV_DeleteLoop(l, &B);
    FreeSignal(sig);
    CHECK(stub_live_allocs() == before);
}

static int zero_count;
static ULONG zero_hook(struct Hook *h, APTR obj, struct EVMessage *m)
{
    zero_count++;
    EV_SetTimer(obj, 0, 0, 0, &B);            /* again at once */
    return 0;
}
static void test_timer_set_zero_in_hook(void)
{
    struct Hook h;
    APTR l;
    setup();
    l = new_loop(FALSE, TRUE);
    hook_init(&h, zero_hook, NULL);
    zero_count = 0;
    EV_AddTimer(l, 0, 0, 0, &h, &B);
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(zero_count == 1);                   /* not again in the same round */
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(zero_count == 2);
    EV_DeleteLoop(l, &B);
}

static struct Hook added_hook;
static ULONG adder_hook(struct Hook *h, APTR obj, struct EVMessage *m)
{
    log_hook(h, obj, m);
    EV_AddTimer(m->em_Loop, 0, 0, 0, &added_hook, &B);   /* added while calling: not this round */
    return 0;
}
static void test_timer_added_in_hook(void)
{
    struct Hook h;
    APTR l;
    setup();
    l = new_loop(FALSE, TRUE);
    hook_init(&h, adder_hook, (APTR)1);
    hook_init(&added_hook, log_hook, (APTR)2);
    EV_Post(l, &h, NULL, &B);                 /* posts are called before timers */
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(nlog == 1);
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(nlog == 2 && logv[1].user == (APTR)2);
    EV_DeleteLoop(l, &B);
}

static ULONG stop_hook(struct Hook *h, APTR obj, struct EVMessage *m)
{
    EV_Stop(m->em_Loop, 42, &B);
    return 0;
}
static void test_run_stop_break(void)
{
    struct Hook hs, hb;
    APTR l;
    setup();
    l = new_loop(TRUE, TRUE);
    hook_init(&hs, stop_hook, NULL);
    EV_AddTimer(l, 1, 0, 0, &hs, &B);
    CHECK(EV_Run(l, &B) == 42);
    CHECK(stub_now == 1001000);
    /* Ctrl-C stops it */
    Signal(NULL, SIGBREAKF_CTRL_C);
    CHECK(EV_Run(l, &B) == EVSTOP_BREAK);
    /* Ctrl-C through the stack's own break mask: EINTR */
    Signal(NULL, SIGBREAKF_CTRL_C);
    stub_select_fail = 4;
    CHECK(EV_Run(l, &B) == EVSTOP_BREAK);
    /* an EV_Stop() before EV_Run() counts */
    EV_Stop(l, 7, &B);
    CHECK(EV_Run(l, &B) == 7);
    EV_DeleteLoop(l, &B);
    /* EVT_BreakStops FALSE: Ctrl-C is the program's */
    l = new_loop(FALSE, FALSE);
    hook_init(&hb, log_hook, (APTR)99);
    EV_AddSignals(l, SIGBREAKF_CTRL_C, &hb, &B);
    nlog = 0;
    Signal(NULL, SIGBREAKF_CTRL_C);
    CHECK(EV_RunOnce(l, NULL, &B) == 1);
    CHECK(nlog == 1 && logv[0].events == SIGBREAKF_CTRL_C);
    CHECK(!((struct EVLoop *)l)->stopped);
    EV_DeleteLoop(l, &B);
}

static void test_wake_and_time(void)
{
    APTR l;
    struct timeval tv;
    setup();
    l = new_loop(FALSE, TRUE);
    EV_Wake(l, &B);
    CHECK(EV_RunOnce(l, NULL, &B) == 0);      /* woke, nothing to call */
    stub_now = 3500001;
    EV_GetTime(&tv, &B);
    CHECK(tv.tv_secs == 3 && tv.tv_micro == 500001);
    /* the timer request is reused while the earliest time stays the same */
    {
        struct Hook h;
        hook_init(&h, log_hook, NULL);
        EV_AddTimer(l, 0, 5000, 0, &h, &B);
        Signal(NULL, 1UL << ((struct EVLoop *)l)->wakesig);
        EV_RunOnce(l, NULL, &B);
        CHECK(((struct EVLoop *)l)->tpending);
        EV_RunOnce(l, NULL, &B);
        CHECK(nlog == 1 && logv[0].at == 3505001);
    }
    EV_DeleteLoop(l, &B);
}

int main(void)
{
    test_timers_in_order();
    test_repeat_and_remove_self();
    test_signals_ports_posts();
    test_sockets();
    test_remove_other_while_calling();
    test_timer_set_zero_in_hook();
    test_timer_added_in_hook();
    test_run_stop_break();
    test_wake_and_time();
    printf("event.library host tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
