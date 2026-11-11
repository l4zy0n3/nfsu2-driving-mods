// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include "steering-guard-core.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
 unsigned attempts=0,verified=0;
 for(uint32_t now=100;steering_guard_retry(1,100,now,attempts);now+=50){
  attempts++;if(attempts==4){verified=1;break;}
 }
 assert(verified&&attempts==4); /* A temporary page failure can recover. */
 assert(!steering_guard_retry(0,100,100,0)); /* Wrong file never gains retry permission. */
 assert(steering_guard_retry(1,100,2099,39));
 assert(!steering_guard_retry(1,100,2100,39));
 assert(!steering_guard_retry(1,100,100,40));
 assert(steering_guard_retry(1,UINT32_MAX-100,50,3));
 assert(!steering_guard_retry(1,UINT32_MAX-100,1900,3));
 puts("PASS steering startup recovery, identity rejection, elapsed/attempt bounds and tick wrap");
}
