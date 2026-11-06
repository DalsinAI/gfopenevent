/* oa_protos_begin.h and oa_protos_end.h: put around a module's proto includes.
 * A copy of amicradle-guest's common/include/oa_protos_begin.h (W80).
 * Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
 *
 *   #include "<path>/include/oa_protos_begin.h"
 *   #include <proto/exec.h>
 *   #include <proto/dos.h>
 *   #include "<path>/include/oa_protos_end.h"
 *
 * The ROM's modules are built with -fno-toplevel-reorder, so that their
 * first function (the "run as a program, do nothing" stub) and their data
 * stay in the order written. With it, GCC 16 keeps every static function,
 * called or not. The NDK's inline headers define a static varargs wrapper
 * for each tag and format call (__SystemTags_va, __OpenWindowTags_va, ...),
 * so a module carried all of them: 7 from dos, 10 from graphics, 10 from
 * intuition, about 22 bytes each. Being in the headers, they also came
 * before the module's own code, so a library file run as a command started
 * in a wrapper instead of its stub, and hung the Shell (W80, 18 Oct 2026).
 *
 * The wrappers carry optimize("O1") (AMIGA_VA_WRAPPER_ATTR, which each
 * inline header defines afresh), and that attribute resets the options a
 * "#pragma GCC optimize" would set. So, between the two headers,
 * optimize(...) also says "toplevel-reorder":
 * - a wrapper nothing calls is dropped;
 * - a wrapper in use is kept, after the code whose order is fixed, so the
 *   stub stays first.
 * Nothing else changes: the module's own functions keep
 * -fno-toplevel-reorder.
 *
 * The begin header must come before any header that includes a proto
 * (a proto's inline header is read only once). */
#ifdef optimize
#error "oa_protos_begin.h: optimize is already a macro (an oa_protos_end.h missing?)"
#endif
#define optimize(...) optimize(__VA_ARGS__, "toplevel-reorder")
