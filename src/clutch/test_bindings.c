// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include "bindings-core.h"
#include <assert.h>
int main(void)
{
    assert(input_kind("Auto")==SOURCE_AUTO&&input_kind("Keyboard")==SOURCE_KEYBOARD);
    assert(input_kind("Gamepad")==SOURCE_GAMEPAD&&input_kind("Wheel")==SOURCE_WHEEL);
    assert(input_kind("garbage")==-1);
    assert(pad_binding("LB")==0x100&&pad_binding("RTHUMB")==0x80);
    assert(pad_binding("garbage")==~0u&&pad_binding("NONE")==0);
    assert(binding_press(pad_binding("LB"),0x100,0,0)==1);
    assert(binding_press(pad_binding("LB"),0,255,255)==0);
    assert(binding_press(BIND_LT,0,255,0)==1&&binding_press(BIND_LT,0,0,255)==0);
    assert(binding_press(BIND_RT,0,0,255)==1);
    assert(binding_press(0,0xffff,255,255)==0&&binding_press(~0u,0xffff,255,255)==0);
    assert(binding_press(BIND_LT,0,128,0)>.50f&&binding_press(BIND_LT,0,128,0)<.51f);
    assert(clutch_axis("X")==0&&clutch_axis("Y")==1&&clutch_axis("SLIDER1")==7);
    assert(clutch_axis("bad")==-1);
    assert(clutch_axis("Rx")==3&&clutch_axis("Ry")==4&&clutch_axis("Rz")==5);
    assert(clutch_axis("S0")==6&&clutch_axis("S1")==7);
    for(unsigned value=0;value<=255;value++){
        assert(throttle_event(0x92,BIND_RT,0,255-value,value)==(0x92|(value<<8)));
        assert(throttle_event(0x92,BIND_LT,0,value,255-value)==(0x92|(value<<8)));
        assert(throttle_event(0x93,BIND_RT,0,0,value)==0x93);
    }
    assert(throttle_event(0x92,pad_binding("A"),0x1000,0,0)==0xff92);
    assert(throttle_event(0x92,pad_binding("A"),0,255,255)==0x92);
    int latch=0;
    assert(!consume_ignition_start(&latch,0,1,1,1));
    assert(!consume_ignition_start(&latch,1,1,0,1));
    assert(consume_ignition_start(&latch,1,1,1,1));
    assert(consume_ignition_start(&latch,1,0,0,1));
    assert(consume_ignition_start(&latch,1,0,0,0)&&!latch);
    assert(!consume_ignition_start(&latch,1,0,0,1));
}
