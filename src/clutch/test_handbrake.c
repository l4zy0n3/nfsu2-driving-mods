// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include "handbrake-core.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    HandbrakeState s={0};
    assert(!handbrake_step(&s,1,0,0));
    assert(handbrake_step(&s,1,1,10));
    assert(handbrake_step(&s,1,0,100)); /* Tap on stays on. */
    assert(handbrake_step(&s,1,0,500));
    assert(handbrake_step(&s,1,1,600));
    assert(!handbrake_step(&s,1,0,700)); /* Second tap releases. */
    assert(handbrake_step(&s,1,1,800));
    assert(handbrake_step(&s,1,1,1150));
    assert(!handbrake_step(&s,1,0,1151)); /* Hold from off never latches. */
    assert(handbrake_step(&s,1,1,1200));
    assert(handbrake_step(&s,1,0,1300));
    assert(handbrake_step(&s,1,1,1400));
    assert(!handbrake_step(&s,1,0,1750)); /* Hold also releases an existing latch. */
    assert(handbrake_step(&s,1,1,1800));
    assert(!handbrake_step(&s,0,1,1850)); /* Focus loss/disconnect clears all state. */
    assert(!handbrake_step(&s,1,1,1900));
    assert(!handbrake_step(&s,1,0,2000));
    assert(handbrake_step(&s,1,1,2100));
    assert(handbrake_step(&s,1,0,2200));
    assert(!handbrake_step(&s,0,0,2300));
    assert(!handbrake_step(&s,1,0,2400));
    assert(handbrake_step(&s,1,1,UINT32_MAX-99));
    assert(!handbrake_step(&s,1,0,250)); /* 350 ms across timer rollover. */
    assert(handbrake_step(&s,1,1,300));
    assert(handbrake_step(&s,1,0,649)); /* 349 ms is a tap. */
    s=(HandbrakeState){0};
    assert(!handbrake_step(&s,1,1,700)); /* Car change while held requires release. */
    assert(!handbrake_step(&s,1,0,800));
    puts("PASS handbrake tap toggle, hold/release, reset, threshold and timer rollover");
}
