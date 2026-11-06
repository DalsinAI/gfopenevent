/* Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * EVTest: event.library on a real Amiga (or a lab copy of one).
 *   EVTest [LIB=path]      opens path (default LIBS:event.library, then
 *                          PROGDIR:event.library)
 * Prints one PASS, FAIL or SKIP line a test and the times it measured;
 * returns 0 when nothing failed, 10 when something did.
 *
 *   timers   three one-shot timers fire in time order, on time
 *   repeat   a 20 ms repeating timer, 10 times: about 200 ms in all
 *   signal   an Exec signal the program allocated
 *   port     a message on a MsgPort, also when its signal was taken
 *   post     EV_Post() from another process wakes the loop
 *   udp      a datagram to itself on 127.0.0.1 makes its socket readable
 *            (SKIP without bsdsocket.library)
 *   idle     a quiet loop with a 500 ms timer uses one wait, not a poll
 *   break    Ctrl-C stops EV_Run() with EVSTOP_BREAK
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <utility/hooks.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/event.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

#define REG(r, decl) register decl __asm(#r)

struct Library *EventBase;
struct Library *SocketBase;
static int failed;

static void result(const char *name, int ok, const char *fmt, long a, long b)
{
    printf("%s %-7s", ok > 0 ? "PASS" : ok == 0 ? "FAIL" : "SKIP", name);
    if (fmt) printf(fmt, a, b);
    printf("\n");
    if (ok == 0) failed = 1;
}

static ULONG ms_since(const struct timeval *t0)
{
    struct timeval t;
    EV_GetTime(&t);
    return (t.tv_secs - t0->tv_secs) * 1000 + ((LONG)t.tv_micro - (LONG)t0->tv_micro) / 1000;
}

/* ---- a Hook that counts and remembers the order it was called in ---------- */

static int order[8], norder;
static ULONG count_hook(REG(a0, struct Hook *h), REG(a2, APTR obj), REG(a1, struct EVMessage *m))
{
    if (norder < 8) order[norder] = (int)(LONG)h->h_Data;
    norder++;
    return 0;
}
static void hook_init(struct Hook *h, APTR fn, LONG data)
{
    h->h_Entry = (HOOKFUNC)fn;
    h->h_SubEntry = NULL;
    h->h_Data = (APTR)data;
}

static void test_timers(APTR loop)
{
    struct Hook h1, h2, h3;
    APTR t1, t2, t3;
    struct timeval t0;
    ULONG ms;
    int i;
    hook_init(&h1, count_hook, 1); hook_init(&h2, count_hook, 2); hook_init(&h3, count_hook, 3);
    norder = 0;
    EV_GetTime(&t0);
    t3 = EV_AddTimer(loop, 0, 60000, 0, &h3);
    t1 = EV_AddTimer(loop, 0, 20000, 0, &h1);
    t2 = EV_AddTimer(loop, 0, 40000, 0, &h2);
    for (i = 0; i < 20 && norder < 3; i++) EV_RunOnce(loop, NULL);
    ms = ms_since(&t0);
    result("timers", norder == 3 && order[0] == 1 && order[1] == 2 && order[2] == 3 && ms >= 59 && ms < 200,
           " 3 fired in order, last at %ld ms (60 asked), %ld rounds", ms, i);
    EV_Remove(t1); EV_Remove(t2); EV_Remove(t3);
}

static int reps;
static ULONG rep_hook(REG(a0, struct Hook *h), REG(a2, APTR obj), REG(a1, struct EVMessage *m))
{
    if (++reps == 10) EV_Stop(m->em_Loop, 10);
    return 0;
}
static void test_repeat(APTR loop)
{
    struct Hook h;
    struct timeval t0;
    APTR t;
    LONG r;
    ULONG ms;
    hook_init(&h, rep_hook, 0);
    reps = 0;
    EV_GetTime(&t0);
    t = EV_AddTimer(loop, 0, 20000, EVTF_REPEAT, &h);
    r = EV_Run(loop);
    ms = ms_since(&t0);
    result("repeat", r == 10 && reps == 10 && ms >= 199 && ms < 400, " 10 x 20 ms in %ld ms (200 asked)%.0ld", ms, 0);
    EV_Remove(t);
}

static void test_signal_port(APTR loop)
{
    struct Hook hs, hp;
    struct MsgPort *port = CreateMsgPort();
    struct Message msg;
    APTR ws, wp;
    BYTE sig = AllocSignal(-1);
    LONG n;
    if (!port || sig < 0) { result("signal", 0, " no port or signal%.0ld%.0ld", 0, 0); return; }
    hook_init(&hs, count_hook, 7); hook_init(&hp, count_hook, 8);
    ws = EV_AddSignals(loop, 1UL << sig, &hs);
    wp = EV_AddPort(loop, port, &hp);
    norder = 0;
    Signal(FindTask(NULL), 1UL << sig);
    n = EV_RunOnce(loop, NULL);
    result("signal", n == 1 && norder == 1 && order[0] == 7, " %ld called%.0ld", n, 0);
    memset(&msg, 0, sizeof(msg));
    msg.mn_Length = sizeof(msg);
    norder = 0;
    PutMsg(port, &msg);
    SetSignal(0, 1UL << port->mp_SigBit);           /* as if someone else's Wait took it */
    n = EV_RunOnce(loop, NULL);
    result("port", n == 1 && norder == 1 && order[0] == 8 && GetMsg(port) == &msg, " %ld called%.0ld", n, 0);
    EV_Remove(ws); EV_Remove(wp);
    DeleteMsgPort(port);
    FreeSignal(sig);
}

/* ---- post from another process -------------------------------------------- */

static APTR post_loop;
static struct Hook post_hook;
static volatile int poster_done;
static void poster(void)
{
    Delay(5);                                       /* the loop is waiting by then */
    EV_Post(post_loop, &post_hook, (APTR)77);
    Forbid();
    poster_done = 1;                                /* exits when it next runs: Forbid holds till then */
}
static APTR posted;
static ULONG post_fn(REG(a0, struct Hook *h), REG(a2, APTR obj), REG(a1, struct EVMessage *m))
{
    posted = m->em_PostData;
    EV_Stop(m->em_Loop, 1);
    return 0;
}
static void test_post(APTR loop)
{
    struct Hook guard;
    APTR g;
    LONG r;
    hook_init(&post_hook, post_fn, 0);
    hook_init(&guard, count_hook, 0);
    post_loop = loop;
    posted = NULL;
    poster_done = 0;
    g = EV_AddTimer(loop, 3, 0, 0, &guard);         /* never wait for ever */
    norder = 0;
    if (!CreateNewProcTags(NP_Entry, (ULONG)poster, NP_Name, (ULONG)"EVTest poster", TAG_DONE)) {
        result("post", 0, " no process%.0ld%.0ld", 0, 0);
        EV_Remove(g);
        return;
    }
    r = EV_Run(loop);
    while (!poster_done) Delay(1);
    Delay(2);
    result("post", r == 1 && posted == (APTR)77 && norder == 0, " data %ld, guard fired %ld", (LONG)posted, norder);
    EV_Remove(g);
}

/* ---- a socket ----------------------------------------------------------------- */

static LONG udp_events;
static ULONG udp_hook(REG(a0, struct Hook *h), REG(a2, APTR obj), REG(a1, struct EVMessage *m))
{
    udp_events = m->em_Events;
    EV_Stop(m->em_Loop, 2);
    return 0;
}
static void test_udp(void)
{
    struct sockaddr_in a;
    LONG s, len = sizeof(a), r = -9;
    struct Hook h, guard;
    APTR loop, w = NULL, g;
    char buf[8];
    struct timeval t0;
    if (!SocketBase) { result("udp", -1, " no bsdsocket.library%.0ld%.0ld", 0, 0); return; }
    loop = EV_CreateLoop(EVT_SocketBase, (ULONG)SocketBase, TAG_DONE);
    s = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(0x7f000001);
    if (!loop || s < 0 || bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 ||
        getsockname(s, (struct sockaddr *)&a, &len) < 0) {
        result("udp", -1, " no loopback socket (errno %ld)%.0ld", Errno(), 0);
        if (s >= 0) CloseSocket(s);
        if (loop) EV_DeleteLoop(loop);
        return;
    }
    hook_init(&h, udp_hook, 0);
    hook_init(&guard, count_hook, 0);
    norder = 0;
    w = EV_AddSocket(loop, s, EVF_READ, &h);
    g = EV_AddTimer(loop, 3, 0, 0, &guard);
    /* nothing to read yet: a 100 ms round calls nobody */
    {
        struct timeval tv;
        tv.tv_secs = 0; tv.tv_micro = 100000;
        EV_RunOnce(loop, &tv);
    }
    if (udp_events) { result("udp", 0, " readable before anything was sent%.0ld%.0ld", 0, 0); goto out; }
    EV_GetTime(&t0);
    sendto(s, (APTR)"ping", 4, 0, (struct sockaddr *)&a, sizeof(a));
    r = EV_Run(loop);
    result("udp", r == 2 && (udp_events & EVF_READ) && recv(s, buf, sizeof(buf), 0) == 4 && norder == 0,
           " readable after %ld ms, events %ld", ms_since(&t0), udp_events);
out:
    EV_Remove(w); EV_Remove(g);
    CloseSocket(s);
    EV_DeleteLoop(loop);
}

/* ---- a quiet loop waits once --------------------------------------------- */

static void test_idle(APTR loop)
{
    struct Hook h;
    struct timeval t0;
    APTR t;
    LONG n, rounds = 0;
    ULONG ms;
    hook_init(&h, count_hook, 0);
    norder = 0;
    EV_GetTime(&t0);
    t = EV_AddTimer(loop, 0, 500000, 0, &h);
    while (!norder && rounds < 100) {
        n = EV_RunOnce(loop, NULL);
        rounds++;
        if (n < 0) break;
    }
    ms = ms_since(&t0);
    result("idle", norder == 1 && rounds == 1 && ms >= 499 && ms < 700, " one wait of %ld ms, %ld round(s)", ms, rounds);
    EV_Remove(t);
}

static void test_break(APTR loop)
{
    struct Hook guard;
    APTR g;
    LONG r;
    hook_init(&guard, count_hook, 0);
    g = EV_AddTimer(loop, 3, 0, 0, &guard);
    norder = 0;
    Signal(FindTask(NULL), SIGBREAKF_CTRL_C);
    r = EV_Run(loop);
    result("break", r == EVSTOP_BREAK && norder == 0, " EV_Run gave %ld%.0ld", r, 0);
    EV_Remove(g);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    APTR loop;
    int i;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "LIB=", 4)) path = argv[i] + 4;
    if (path) EventBase = OpenLibrary((STRPTR)path, EVENT_VERSION);
    else if (!(EventBase = OpenLibrary((STRPTR)"LIBS:event.library", EVENT_VERSION)))
        EventBase = OpenLibrary((STRPTR)"PROGDIR:event.library", EVENT_VERSION);
    if (!EventBase) { printf("FAIL open   can't open event.library %ld\n", (long)EVENT_VERSION); return 10; }
    printf("%s", (char *)EventBase->lib_IdString);
    SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
    loop = EV_CreateLoopA(NULL);
    if (!loop) { printf("FAIL create EV_CreateLoopA\n"); return 10; }
    test_timers(loop);
    test_repeat(loop);
    test_signal_port(loop);
    test_post(loop);
    test_udp();
    test_idle(loop);
    test_break(loop);
    EV_DeleteLoop(loop);
    if (SocketBase) CloseLibrary(SocketBase);
    CloseLibrary(EventBase);
    printf("%s\n", failed ? "EVTest: FAILED" : "EVTest: all passed");
    return failed ? 10 : 0;
}
