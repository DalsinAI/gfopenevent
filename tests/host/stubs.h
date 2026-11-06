/* Copyright (c) 2026 AmiCradle Team. GFOpenEvent, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * tests/host: what stubs.c gives the tests besides exec's own calls. */
#ifndef EVENT_TEST_STUBS_H
#define EVENT_TEST_STUBS_H
#include "../../library/evsys.h"
#include "../../include/libraries/event.h"

#define STUB_FDS 64
extern ev_u64 stub_now;
extern ULONG stub_sigs;
extern ULONG stub_ready[STUB_FDS];
extern LONG stub_errno, stub_select_fail;
extern int stub_waits, stub_selects, stub_polls;
long stub_live_allocs(void);
#endif
