# event.library 1.1

`#include <proto/event.h>`, base `EventBase`, `OpenLibrary("event.library", 1)`.

## Rules for every call

- A **loop** belongs to the task that made it. Only that task adds watches
  to it, changes them, runs it and deletes it. Other tasks may call only
  `EV_Wake()`, `EV_Stop()` and `EV_Post()`.
- A **watch** is what an `EV_Add...` call returns. `EV_Remove()` ends any
  kind. `EV_DeleteLoop()` frees the watches still there.
- **Hooks** are called the standard way (as `utility.library`'s
  `CallHookPkt()`): A0 the `struct Hook`, A2 the watch, A1 a
  `struct EVMessage` (`libraries/event.h`). `em_UserData` is the Hook's
  `h_Data`. The message is good until the Hook returns.
- A Hook may add, change and remove any watch, its own too. A watch removed
  while Hooks are being called is never called again and is freed when the
  round ends; one added is first looked at in the next round.
- A Hook must not call `EV_RunOnce()`, `EV_Run()` or `EV_DeleteLoop()` on
  its own loop.

## The calls

**`loop = EV_CreateLoopA(tags)`**, `EV_CreateLoop(tag, ...)`
A new loop for the calling task, or NULL. It takes one signal and one
`timer.device` request (UNIT_MICROHZ). Tags:
- `EVT_SocketBase` (`struct Library *`): the program's own
  `bsdsocket.library` base. A socket belongs to the base that made it, so
  the loop calls `WaitSelect()` through this one. Without it the loop has
  no sockets and waits with `Wait()`.
- `EVT_BreakStops` (BOOL, default TRUE): Ctrl-C stops `EV_Run()` with
  `EVSTOP_BREAK`. With FALSE, Ctrl-C is the program's, to watch with
  `EV_AddSignals()` if it wants it.

**`EV_DeleteLoop(loop)`**
Ends the timer request and frees the loop, its watches and any posts not
yet called. NULL is allowed.

**`watch = EV_AddSocket(loop, fd, events, hook)`**
Calls the Hook when socket `fd` (of the loop's socket base) is ready for
`events`: any of `EVF_READ`, `EVF_WRITE`, `EVF_EXCEPT`. `em_Events` says
which are ready. NULL when the loop has no socket base. A watch with no
events stays but isn't waited for. More than one watch may name one
descriptor.

**`EV_SetSocketEvents(watch, events)`**
What a socket watch waits for from now on (0: nothing for now).

**`watch = EV_AddSignals(loop, mask, hook)`**
Calls the Hook when any signal of `mask` arrives; `em_Events` holds the
ones that did. The loop takes them (as `Wait()` does).

**`watch = EV_AddPort(loop, port, hook)`**
Calls the Hook while `port` has messages, and the Hook takes them with
`GetMsg()`. The port is looked at every round whether its signal came or
not, so a message whose signal someone else took, or a port that shares
its signal, is not missed. A Hook that leaves a message on the port is
called again in the next round, without a wait between.

**`watch = EV_AddTimer(loop, secs, micros, flags, hook)`**
A timer that fires `secs` seconds and `micros` microseconds from now, and
with `EVTF_REPEAT` every such interval after (counted from when it was
due, so it doesn't drift; a loop that fell behind skips what it missed).
Timers due together fire in the order they were set.

**`EV_SetTimer(watch, secs, micros, flags)`**
Sets a timer again, from now, stopped or not.

**`EV_StopTimer(watch)`**
Stops a timer; the watch stays, for `EV_SetTimer()`.

**`EV_Remove(watch)`**
Ends a watch of any kind. NULL is allowed.

**`n = EV_RunOnce(loop, timeout)`**
One round: one wait, then the Hooks of what is ready. `timeout` NULL waits
until something is ready; `{0, 0}` doesn't wait; else at most that long.
It doesn't wait at all when a post is queued, a port has a message or a
timer is due. Returns the number of Hooks called, or:
- `EVERR_TASK`: not the loop's own task;
- `EVERR_SELECT`: `WaitSelect()` failed other than with EINTR (its
  `Errno()` is the program's to read);
- `EVERR_NOMEM`.
A Ctrl-C under `EVT_BreakStops` marks the loop stopped, for `EV_Run()`.

**`result = EV_Run(loop)`**
Rounds until `EV_Stop()` (its code) or Ctrl-C (`EVSTOP_BREAK`), or an
`EV_RunOnce()` error. An `EV_Stop()` before `EV_Run()` counts: it returns
at once.

**`EV_Stop(loop, code)`**
`EV_Run()` returns `code` after this round. Any task may call it.

**`EV_Wake(loop)`**
Ends the loop's wait, from any task (the round then calls whatever is
ready, maybe nothing).

**`ok = EV_Post(loop, hook, data)`**
From any task: the loop's task calls `hook` with `em_Kind` `EVK_POST`,
`em_PostData` `data` and `em_Watch` NULL, in its next round, in the order
posted. FALSE when out of memory. Not from interrupts.

**`EV_GetTime(tv)`**
The time from the E-clock, in seconds and microseconds: it never goes
back, unlike the system clock. The timers count on it.
