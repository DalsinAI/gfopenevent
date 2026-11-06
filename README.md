# GFOpenEvent

One event loop for Amiga programs (AmigaOS 3): `event.library` waits once,
with `WaitSelect()` or `Wait()`, for a program's sockets, signals,
MsgPorts, timers and Ctrl-C, and calls it back for what is ready.

MIT ([`LICENSE`](LICENSE)). Designed, tested and delivered by The AmiCradle Team.
