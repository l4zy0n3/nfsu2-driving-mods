// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include <math.h>
#include <stdint.h>
#include <string.h>
static float stick_axis(int16_t n)
{
    float v=n<0?n/32768.0f:n/32767.0f;
    return fabsf(v)<=.18f?0:copysignf((fabsf(v)-.18f)/.82f,v);
}
static int orbit(float out[3],const float pivot[3],const float point[3],float yaw,float pitch)
{
    float v[3];
    for(int i=0;i<3;i++){
        if(!isfinite(pivot[i])||!isfinite(point[i]))return 0;
        v[i]=point[i]-pivot[i];
    }
    float r=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(!isfinite(r)||r<.01f||r>10000||!isfinite(yaw)||!isfinite(pitch))return 0;
    if(yaw==0&&pitch==0){memcpy(out,point,3*sizeof(float));return 1;}
    float angle=atan2f(v[1],v[0])+yaw;
    float elevation=fminf(1.35f,fmaxf(-1.35f,atan2f(v[2],hypotf(v[0],v[1]))+pitch));
    out[0]=pivot[0]+r*cosf(elevation)*cosf(angle);
    out[1]=pivot[1]+r*cosf(elevation)*sinf(angle);
    out[2]=pivot[2]+r*sinf(elevation);
    return 1;
}

/* Linux writes this fixed-size little-endian file in place, guarded by sequence. */
typedef struct {
    uint32_t sequence,magic,version,flags;
    uint64_t unix_ms;
    float yaw,pitch;
} GyroPacket;
_Static_assert(sizeof(GyroPacket)==32,"gyro wire size");
typedef struct {uint32_t sequence,changed_ms;int seen;} GyroFreshness;
static float unit_clamp(float n){return fminf(1,fmaxf(-1,n));}
static int gyro_decode(const GyroPacket *p,uint32_t before,uint32_t after,
                       uint64_t utc_ms,uint32_t now_ms,GyroFreshness *state,float out[2])
{
    out[0]=out[1]=0;
    if((before&1)||before!=after||p->sequence!=before||p->magic!=0x3247594e||
       p->version!=1||!(p->flags&1)||!isfinite(p->yaw)||!isfinite(p->pitch))return 0;
    if(p->unix_ms>utc_ms ? p->unix_ms-utc_ms>50 : utc_ms-p->unix_ms>250)return 0;
    if(!state->seen||state->sequence!=before){
        state->sequence=before;state->changed_ms=now_ms;state->seen=1;
    }
    if((uint32_t)(now_ms-state->changed_ms)>250)return 0;
    out[0]=unit_clamp(p->yaw);out[1]=unit_clamp(p->pitch);
    return 1;
}

#ifdef SELF_TEST
#include <assert.h>
int main(void)
{
    assert(stick_axis(0)==0&&stick_axis(5000)==0);
    assert(fabsf(stick_axis(32767)-1)<.00001f&&fabsf(stick_axis(-32768)+1)<.00001f);
    float pivot[3]={0},point[3]={5,0,0},out[3];
    assert(orbit(out,pivot,point,0,0)&&!memcmp(out,point,sizeof(point)));
    assert(orbit(out,pivot,point,1.5707963268f,0)&&fabsf(out[0])<.00001f&&fabsf(out[1]-5)<.00001f);
    assert(orbit(out,pivot,point,3.1415926536f,0)&&fabsf(out[0]+5)<.00001f);
    assert(orbit(out,pivot,point,1,100)&&isfinite(out[2]));
    assert(fabsf(sqrtf(out[0]*out[0]+out[1]*out[1]+out[2]*out[2])-5)<.00001f);
    assert(!orbit(out,pivot,pivot,0,0));
    point[0]=NAN;assert(!orbit(out,pivot,point,0,0));
    GyroFreshness freshness={0};float g[2];
    GyroPacket packet={2,0x3247594e,1,1,1000,2,-2};
    assert(gyro_decode(&packet,2,2,1000,10,&freshness,g)&&g[0]==1&&g[1]==-1);
    assert(gyro_decode(&packet,2,2,1250,260,&freshness,g));
    assert(!gyro_decode(&packet,2,2,1251,261,&freshness,g)&&g[0]==0&&g[1]==0);
    assert(!gyro_decode(&packet,2,2,949,20,&freshness,g));
    assert(!gyro_decode(&packet,3,3,1000,20,&freshness,g));
    assert(!gyro_decode(&packet,2,4,1000,20,&freshness,g));
    packet.flags=0;assert(!gyro_decode(&packet,2,2,1000,20,&freshness,g));
    packet.flags=1;packet.yaw=NAN;assert(!gyro_decode(&packet,2,2,1000,20,&freshness,g));
    packet.yaw=.2f;packet.version=2;assert(!gyro_decode(&packet,2,2,1000,20,&freshness,g));
    packet.version=1;packet.magic=0;assert(!gyro_decode(&packet,2,2,1000,20,&freshness,g));
    packet.magic=0x3247594e;packet.unix_ms=10000;
    assert(!gyro_decode(&packet,2,2,10000,261,&freshness,g)); /* frozen sequence despite fresh clock */
    packet.sequence=4;assert(gyro_decode(&packet,4,4,10000,262,&freshness,g));
    return 0;
}
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include "../common/runtime-paths.h"
typedef DWORD (WINAPI *PadFn)(DWORD,XINPUT_STATE *);
typedef void (__cdecl *LookAt)(float *,const float *,const float *,const float *);
static PadFn read_pad;
static float smooth_x,smooth_y;
static DWORD last_time,last_log,gamepad_index;

static const GyroPacket *gyro_view;
static GyroFreshness gyro_freshness;
static DWORD gyro_retry,gyro_setting_time;
static int gyro_attempted,gyro_setting_seen,gyro_enabled=1;
static char gyro_ini[MAX_PATH],log_path[MAX_PATH];
static void gyro_read(DWORD now,float out[2])
{
    out[0]=out[1]=0;
    if(!gyro_setting_seen||(DWORD)(now-gyro_setting_time)>=1000){
        gyro_enabled=GetPrivateProfileIntA("Look","GyroEnabled",1,gyro_ini)!=0;
        gyro_setting_time=now;gyro_setting_seen=1;
    }
    if(!gyro_enabled){gyro_freshness.seen=0;return;}
    if(!gyro_view&&(!gyro_attempted||(DWORD)(now-gyro_retry)>=1000)){
        gyro_attempted=1;gyro_retry=now;
        char path[MAX_PATH];if(!runtime_path("NFSU2_GYRO_PATH",path))return;
        HANDLE file=CreateFileA(path,GENERIC_READ,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
        if(file!=INVALID_HANDLE_VALUE){
            LARGE_INTEGER length;
            if(GetFileSizeEx(file,&length)&&length.QuadPart>=32){
                HANDLE map=CreateFileMappingA(file,NULL,PAGE_READONLY,0,32,NULL);
                if(map){gyro_view=MapViewOfFile(map,FILE_MAP_READ,0,0,32);CloseHandle(map);}
            }
            CloseHandle(file);
        }
    }
    if(!gyro_view)return;
    uint32_t before=*(const volatile uint32_t *)&gyro_view->sequence;
    MemoryBarrier();
    GyroPacket packet;memcpy(&packet,gyro_view,sizeof(packet));
    MemoryBarrier();
    uint32_t after=*(const volatile uint32_t *)&gyro_view->sequence;
    FILETIME filetime;ULARGE_INTEGER utc;
    GetSystemTimeAsFileTime(&filetime);utc.LowPart=filetime.dwLowDateTime;utc.HighPart=filetime.dwHighDateTime;
    uint64_t utc_ms=utc.QuadPart/10000-11644473600000ULL;
    gyro_decode(&packet,before,after,utc_ms,now,&gyro_freshness,out);
}

static void log_line(const char *s)
{
    FILE *f=fopen(log_path,"a");
    if(f){fprintf(f,"%s\n",s);fclose(f);}
}
static int readable(const void *p,SIZE_T n)
{
    MEMORY_BASIC_INFORMATION m;
    return p&&VirtualQuery(p,&m,sizeof(m))&&m.State==MEM_COMMIT&&!(m.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&
        (uintptr_t)p+n<=(uintptr_t)m.BaseAddress+m.RegionSize;
}
__attribute__((used,noinline)) static void __cdecl look_camera(char *mover,float *out,const float *eye,const float *target,const float *up)
{
    const float *new_eye=eye,*new_target=target;
    float adjusted[3];DWORD foreground=0,now=GetTickCount();
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    int mode=readable(mover,0x60)?*(int *)(mover+0x4c):-1;
    int main=mode>=0&&mode<=5&&*(void **)(mover+0x1c)==*(void **)0x832e90;
    if(main){
        XINPUT_STATE pad;int valid=0;
        if(foreground==GetCurrentProcessId()&&!*(unsigned char *)(mover+0x51)&&read_pad)
            valid=read_pad(gamepad_index,&pad)==ERROR_SUCCESS;
        if(valid&&readable(eye,12)&&readable(target,12)&&readable(up,12)){
            float dt=last_time?fminf(.1f,(now-last_time)/1000.0f):.016f;
            float alpha=1-expf(-dt/.08f);
            smooth_x+=(stick_axis(pad.Gamepad.sThumbRX)-smooth_x)*alpha;
            smooth_y+=(stick_axis(pad.Gamepad.sThumbRY)-smooth_y)*alpha;
            if(fabsf(smooth_x)<.001f)smooth_x=0;
            if(fabsf(smooth_y)<.001f)smooth_y=0;
            float gyro[2];gyro_read(now,gyro);
            if(smooth_x!=0||smooth_y!=0||gyro[0]!=0||gyro[1]!=0){
                int chase=mode==0||mode==1||mode==5;
                float yaw=-smooth_x*3.1415926536f,pitch=smooth_y*.45f;
                if(gyro[0]!=0||gyro[1]!=0){
                    yaw=-unit_clamp(smooth_x+gyro[0])*3.1415926536f;
                    pitch=unit_clamp(smooth_y+gyro[1])*.45f;
                }
                if(orbit(adjusted,chase?target:eye,chase?eye:target,yaw,pitch)){
                    if(chase)new_eye=adjusted;else new_target=adjusted;
                    if(now-last_log>1000){
                        char s[160];snprintf(s,sizeof(s),"applied=1 view=%d RX=%d RY=%d gyro_x=%.4f gyro_y=%.4f",mode,pad.Gamepad.sThumbRX,pad.Gamepad.sThumbRY,gyro[0],gyro[1]);
                        log_line(s);last_log=now;
                    }
                }
            }
        }else smooth_x=smooth_y=0;
        last_time=now;
    }
    ((LookAt)0x5ba5e0)(out,new_eye,new_target,up);
}
__attribute__((naked)) static void look_hook(void)
{
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\t"
        "pushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tpushl %esi\n\t"
        "call _look_camera\n\taddl $20,%esp\n\tpopl %ebp\n\tret\n\t");
}
static DWORD WINAPI install(void *unused)
{
    (void)unused;
    DWORD n=GetModuleFileNameA(NULL,gyro_ini,sizeof(gyro_ini));
    char *slash=n&&n<sizeof(gyro_ini)?strrchr(gyro_ini,'\\'):NULL;
    if(!slash||(size_t)(slash-gyro_ini)+50>=sizeof(gyro_ini))return 0;
    strcpy(slash,"\\scripts\\NFSU2.RightStickLook.log");strcpy(log_path,gyro_ini);
    strcpy(slash,"\\scripts\\NFSU2.Realism.ini");
    gamepad_index=GetPrivateProfileIntA("Controls","GamepadIndex",0,gyro_ini);
    if(gamepad_index>=XUSER_MAX_COUNT){log_line("not_installed=invalid_gamepad_index");return 0;}
    strcpy(slash,"\\scripts\\NFSU2.RightStickLook.ini");
    unsigned char *base=(void *)GetModuleHandleA(NULL),*site=(void *)0x453af4;
    const unsigned char expected[5]={0xe8,0xe7,0x6a,0x16,0};
    if((uintptr_t)base!=0x400000||memcmp(site,expected,5)){
        log_line("not_installed=game_build_or_hook_guard");return 0;
    }
    IMAGE_NT_HEADERS *nt=(void *)(base+((IMAGE_DOS_HEADER *)base)->e_lfanew);
    if(nt->OptionalHeader.AddressOfEntryPoint!=0x35b8d1){log_line("not_installed=entry_guard");return 0;}
    HMODULE input=LoadLibraryA("xinput1_4.dll");
    read_pad=input?(PadFn)(void *)GetProcAddress(input,"XInputGetState"):NULL;
    if(!read_pad){log_line("not_installed=xinput_missing");return 0;}
    DWORD old,ignored;
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old)){log_line("not_installed=protection");return 0;}
    /* Only the rel32 changes; this four-byte store fits within one cache line. */
    int32_t delta=(int32_t)((uintptr_t)look_hook-(uintptr_t)(site+5));
    memcpy(site+1,&delta,4);
    FlushInstructionCache(GetCurrentProcess(),site,5);
    VirtualProtect(site,5,old,&ignored);
    log_line("installed=1 right_stick=look native_bindings_preserved=1 native_collision_preserved=1 gyro_mailbox=1");
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void *unused)
{
    (void)unused;
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(module);HANDLE t=CreateThread(NULL,0,install,NULL,0,NULL);if(t)CloseHandle(t);}
    return TRUE;
}
#endif
