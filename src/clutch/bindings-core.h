// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_BINDINGS_CORE_H
#define U2_BINDINGS_CORE_H
#include <string.h>

enum { SOURCE_AUTO, SOURCE_KEYBOARD, SOURCE_GAMEPAD, SOURCE_WHEEL };
static int input_kind(const char *name)
{
    const char *names[]={"Auto","Keyboard","Gamepad","Wheel"};
    for(int i=0;i<4;i++)if(!strcmp(name,names[i]))return i;
    return -1;
}
/* XInput masks. Triggers have distinct values outside the button mask. */
enum { BIND_LT=0x10000, BIND_RT=0x20000 };
static unsigned pad_binding(const char *name)
{
    static const struct {const char *name;unsigned mask;} names[]={
        {"NONE",0},{"UP",1},{"DOWN",2},{"LEFT",4},{"RIGHT",8},
        {"START",0x10},{"BACK",0x20},{"LTHUMB",0x40},{"RTHUMB",0x80},
        {"LB",0x100},{"RB",0x200},{"A",0x1000},{"B",0x2000},
        {"X",0x4000},{"Y",0x8000},{"LT",BIND_LT},{"RT",BIND_RT}};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)
        if(!strcmp(name,names[i].name))return names[i].mask;
    return ~0u;
}
static float binding_press(unsigned binding,unsigned buttons,unsigned lt,unsigned rt)
{
    if(binding==BIND_LT)return (lt>255?255:lt)/255.0f;
    if(binding==BIND_RT)return (rt>255?255:rt)/255.0f;
    return binding&&binding<=0xffff&&(buttons&binding)==binding?1.0f:0.0f;
}
static int clutch_axis(const char *name)
{
    const char *axes[]={"X","Y","Z","RX","RY","RZ","SLIDER0","SLIDER1"};
    for(int i=0;i<8;i++)if(!strcmp(name,axes[i]))return i;
    return -1;
}
static int consume_ignition_start(int *latched,int uses_start,int ignition,int clutch_down,int start_down)
{
    if(uses_start&&ignition&&clutch_down)*latched=1;
    int consumed=*latched;
    if(!start_down)*latched=0;
    return consumed;
}
#endif
