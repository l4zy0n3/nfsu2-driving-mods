// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_HANDBRAKE_CORE_H
#define U2_HANDBRAKE_CORE_H
#include <stdint.h>
enum { HANDBRAKE_HOLD_MS = 350 };
typedef struct { int armed, down, latched; uint32_t since; } HandbrakeState;
static int handbrake_step(HandbrakeState *s,int valid,int pressed,uint32_t now)
{
    if(!valid){*s=(HandbrakeState){0};return 0;}
    /* A held button on reconnect or focus recovery must not latch the brake. */
    if(!s->armed){if(!pressed)s->armed=1;return 0;}
    if(pressed&&!s->down)s->since=now;
    if(s->down&&!pressed){
        s->latched=(uint32_t)(now-s->since)<HANDBRAKE_HOLD_MS?!s->latched:0;
    } else if(pressed&&(uint32_t)(now-s->since)>=HANDBRAKE_HOLD_MS){
        s->latched=0;
    }
    s->down=pressed;
    return pressed||s->latched;
}
#endif
