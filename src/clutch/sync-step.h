// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_SYNC_STEP_H
#define U2_SYNC_STEP_H
#include <math.h>
typedef struct {double pending;unsigned captures,consumes;} SyncStepClock;
static void sync_step_capture(SyncStepClock *clock,double dt)
{
 /* ResolveRest calls component posts again with zero dt. Preserve the real step. */
 if(isfinite(dt)&&dt>0&&dt<=.1){clock->pending=dt;clock->captures++;}
}
static double sync_step_consume(SyncStepClock *clock)
{
 double dt=clock->pending;clock->pending=0;
 if(dt>0)clock->consumes++;
 return dt;
}
static void sync_step_reset(SyncStepClock *clock){clock->pending=0;}
#endif
