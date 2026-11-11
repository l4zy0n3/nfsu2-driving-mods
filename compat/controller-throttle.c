// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include <stdint.h>
static unsigned throttle_event(unsigned event,uint8_t right_trigger)
{return event|((event==0x92?(unsigned)right_trigger:0)<<8);}
#ifdef SELF_TEST
#include <assert.h>
int main(void)
{
    assert(throttle_event(0x92,0)==0x92);
    assert(throttle_event(0x92,128)==0x8092);
    assert(throttle_event(0x92,255)==0xff92);
    assert(throttle_event(0x93,0)==0x93&&throttle_event(0x93,255)==0x93);
    for(unsigned value=0;value<=255;value++){
        assert((throttle_event(0x92,(uint8_t)value)>>8)==value);
        assert(throttle_event(0x93,(uint8_t)value)==0x93);
    }
    return 0;
}
#else
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <xinput.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
typedef struct {uint32_t type,unused,event;void *scan;uint32_t binding,range,param,key;} Scanner;
_Static_assert(sizeof(Scanner)==32&&offsetof(Scanner,scan)==12,"native scanner ABI");
typedef int (__cdecl *ScanFn)(void *,unsigned *,unsigned,Scanner *,void *);
typedef DWORD (WINAPI *PadFn)(DWORD,XINPUT_STATE *);
static PadFn read_pad;
static int wheel_present,had_pad;
static DWORD last_log;
static char log_path[MAX_PATH];
static void log_line(const char *s)
{
    FILE *f=fopen(log_path,"a");
    if(f){fprintf(f,"%s\n",s);fclose(f);}
}
static BOOL CALLBACK wheel_device(const DIDEVICEINSTANCEA *device,void *unused)
{
    (void)unused;
    if(strstr(device->tszProductName,"G923")||strstr(device->tszInstanceName,"G923"))wheel_present=1;
    return DIENUM_CONTINUE;
}
static int __cdecl scan_throttle(void *node,unsigned *unknown,unsigned flags,Scanner *config,void *joystick)
{
    if(joystick!=(void *)0x8655a0||wheel_present||
       (config!=(Scanner *)0x7fe190&&config!=(Scanner *)0x7fe1b0))
        return ((ScanFn)0x579150)(node,unknown,flags,config,joystick);
    XINPUT_STATE pad;
    int valid=read_pad(0,&pad)==ERROR_SUCCESS;
    if(!valid&&!had_pad)return ((ScanFn)0x579150)(node,unknown,flags,config,joystick);
    had_pad|=valid;
    uint8_t right=valid?pad.Gamepad.bRightTrigger:0;
    if(config->event==0x92&&GetTickCount()-last_log>1000){
        char s[160];snprintf(s,sizeof(s),"controller=%d R2=%u L2=%u analog_throttle=%u alternate_throttle=0 native_pedal_logic=1",
            valid,right,valid?pad.Gamepad.bLeftTrigger:0,right);
        log_line(s);last_log=GetTickCount();
    }
    return (int)throttle_event(config->event,right);
}
static DWORD WINAPI install(void *unused)
{
    (void)unused;
    DWORD n=GetModuleFileNameA(NULL,log_path,sizeof(log_path));
    char *slash=n&&n<sizeof(log_path)?strrchr(log_path,'\\'):NULL;
    if(!slash||(size_t)(slash-log_path)+48>=sizeof(log_path))return 0;
    strcpy(slash,"\\scripts\\NFSU2.ControllerThrottle.log");
    FILE *log=fopen(log_path,"w");if(log)fclose(log);
    unsigned char *base=(void *)GetModuleHandleA(NULL);
    if((uintptr_t)base!=0x400000){log_line("not_installed=image_guard");return 0;}
    IMAGE_NT_HEADERS *nt=(void *)(base+((IMAGE_DOS_HEADER *)base)->e_lfanew);
    const unsigned char code[10]={0x8b,0x4c,0x24,0x14,0x55,0x56,0x8b,0x74,0x24,0x18};
    const uint32_t expected[2][8]={
        {0xff,0,0x92,0x579150,0x00400001,0x300,0,0x7a2d44},
        {0xff,0,0x93,0x579150,0x00580001,0x301,0,0x7a2d44}};
    Scanner *records[2]={(void *)0x7fe190,(void *)0x7fe1b0};
    if(nt->OptionalHeader.AddressOfEntryPoint!=0x35b8d1||memcmp((void *)0x579150,code,10)||
       memcmp(records[0],expected[0],32)||memcmp(records[1],expected[1],32)){
        log_line("not_installed=native_scanner_build_guard");return 0;
    }
    HMODULE input=LoadLibraryA("xinput1_4.dll");
    read_pad=input?(PadFn)(void *)GetProcAddress(input,"XInputGetState"):NULL;
    if(!read_pad){log_line("not_installed=xinput_missing");return 0;}
    IDirectInput8A *direct=NULL;
    if(FAILED(DirectInput8Create(GetModuleHandleA(NULL),DIRECTINPUT_VERSION,&IID_IDirectInput8A,(void **)&direct,NULL))){
        log_line("not_installed=wheel_profile_unknown");return 0;
    }
    HRESULT result=IDirectInput8_EnumDevices(direct,DI8DEVCLASS_GAMECTRL,wheel_device,NULL,DIEDFL_ATTACHEDONLY);
    IDirectInput8_Release(direct);
    if(FAILED(result)||wheel_present){log_line("not_installed=G923_profile_retains_native_scanners");return 0;}
    /* shortcut: choose the wheel/pad profile at launch; relaunch after changing hardware. */
    DWORD old,ignored;
    if(!VirtualProtect(records[0],64,PAGE_READWRITE,&old)){log_line("not_installed=protection");return 0;}
    InterlockedExchangePointer(&records[0]->scan,(void *)scan_throttle);
    InterlockedExchangePointer(&records[1]->scan,(void *)scan_throttle);
    int ok=records[0]->scan==(void *)scan_throttle&&records[1]->scan==(void *)scan_throttle;
    VirtualProtect(records[0],64,old,&ignored);
    log_line(ok?"installed=1 input_layer=XInput_R2 primary_analog=0x92 alternate_zero=0x93 native_transmission_unchanged=1":"install_readback_failed=1");
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void *unused)
{
    (void)unused;
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(module);HANDLE thread=CreateThread(NULL,0,install,NULL,0,NULL);if(thread)CloseHandle(thread);}
    return TRUE;
}
#endif
