// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_WHEEL_BUTTONS_H
#define U2_WHEEL_BUTTONS_H
#include <stdint.h>
typedef struct { int armed, timing, fired, camera, pause, look; uint32_t since; } WheelButtonState;
typedef struct { int toggle, camera, pause, look; } WheelButtonActions;
static WheelButtonActions wheel_buttons_step(WheelButtonState *s,int valid,int focused,
    int a,int b,int camera,int look,int pause,uint32_t now,uint32_t hold)
{
    WheelButtonActions out={0};
    if(!valid||!focused){*s=(WheelButtonState){0};return out;}
    /* After reconnect/focus recovery, require a clean release before taking actions. */
    if(!s->armed){if(!a&&!b&&!camera&&!look&&!pause)s->armed=1;return out;}
    if(!a&&!b){s->fired=0;s->timing=0;}
    else if(a&&b&&!s->fired){
        if(!s->timing){s->timing=1;s->since=now;}
        if((uint32_t)(now-s->since)>=hold){out.toggle=1;s->fired=1;}
    } else s->timing=0;
    out.camera=camera&&!s->camera;out.pause=pause&&!s->pause;
    out.look=look&&!out.pause;
    s->camera=camera;s->pause=pause;s->look=out.look;
    return out;
}
#endif
