# GFOpenEvent

One event loop for Amiga programs, as part of the GFOpen family for
AmigaOS 3. `event.library` waits **once** for everything a program waits
on, and calls the program back for what is ready:

- **sockets**, through the program's own `bsdsocket.library` base and
  `WaitSelect()`;
- **Exec signals** the program allocated;
- **MsgPorts**: Intuition window ports, ARexx, AppMessage, commodities,
  device replies;
- **timers**, one-shot or repeating, all on one `timer.device` request a
  loop;
- **Ctrl-C**;
- **posts** from other tasks (`EV_Post`), and a plain wake-up (`EV_Wake`).

`bsdsocket.library` is unchanged: `WaitSelect()` already takes an Exec
signal mask, so one call covers sockets and signals. Without a socket base
the loop waits with Exec's `Wait()`.

It is the shared base for:
1. **Boost.Asio's Amiga reactor** in [gfopenboost](https://github.com/DalsinAI/gfopenboost),
   so Asio programs (aMule now) stop polling;
2. **the wx toolkit's event loop** in [gfopenwxtoolkit](https://github.com/DalsinAI/gfopenwxtoolkit),
   whose own `WaitSelect()` loop can move onto it;
3. plain C programs and SDL ports that today write their own loop.

## Status, 6 November 2026

**1.1.** 1.0 was the first version; 1.1 adds the `$VER:` string, so
`Version LIBS:event.library` shows it.

| Part | State |
| --- | --- |
| `include/libraries/event.h`, `library/event_lib.sfd`, the `clib`, `proto` and `inline` headers | 1.1; the headers are made from the SFD by `tools/gen-headers.sh` |
| `library/event_lib.c` | The loop: sockets, signals, ports, timers, posts, Ctrl-C |
| `tests/host/` | The loop on the host with stubs for exec, timer.device, utility and bsdsocket: 110 checks |
| `tests/amiga/evtest.c` (`EVTest`) | The loop on an Amiga: timers, repeat, signal, port, post from another process, UDP to itself, one wait when idle, Ctrl-C |

## Building

    library/build.sh [OUT_DIR]          # event.library and EVTest, os32 stove
    CC=.../m68k-amigaos-gcc EXTRA_INC=.../gfopensdk/OpenNDK/Include_H library/build.sh
    NDK=.../ndk-include tests/host/run.sh

68040, soft float (`specs/m68k-soft-m68040.specs`). The library has no
floating point; it is bare (no startup code, no C library) and needs only
exec, utility.library 37 and timer.device.

## Using it

```c
#include <proto/event.h>

static ULONG on_window(REG(a0, struct Hook *h), REG(a2, APTR watch), REG(a1, struct EVMessage *m))
{
    struct IntuiMessage *im;
    while ((im = (struct IntuiMessage *)GetMsg(m->em_Port)) != NULL) {
        /* ... */
        ReplyMsg(&im->ExecMessage);
    }
    return 0;
}

loop = EV_CreateLoop(EVT_SocketBase, (ULONG)SocketBase, TAG_DONE);
EV_AddPort(loop, window->UserPort, &window_hook);
EV_AddSocket(loop, sock, EVF_READ, &socket_hook);
EV_AddTimer(loop, 1, 0, EVTF_REPEAT, &tick_hook);
result = EV_Run(loop);                 /* until EV_Stop() or Ctrl-C */
EV_DeleteLoop(loop);
```

The calls are described in [`AutoDocs-GFOpenEvent.md`](AutoDocs-GFOpenEvent.md).

## Licence

MIT ([`LICENSE`](LICENSE)). Designed, tested and delivered by The AmiCradle Team.
