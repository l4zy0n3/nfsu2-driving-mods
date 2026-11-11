// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_STEERING_GUARD_CORE_H
#define U2_STEERING_GUARD_CORE_H
#include <stdint.h>
/* Bound both elapsed time and attempts; an identity mismatch never retries. */
static int steering_guard_retry(int transient,uint32_t began,uint32_t now,unsigned attempts)
{return transient&&now-began<2000&&attempts<40;}
#endif
