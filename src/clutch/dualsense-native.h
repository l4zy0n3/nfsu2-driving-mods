// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_DUALSENSE_NATIVE_H
#define U2_DUALSENSE_NATIVE_H
#include "dualsense-core.h"
#include "car-profile.h"
static CRITICAL_SECTION dualsense_lock;
static struct {int enabled;DualSenseSample sample;DualSenseKick kick;} dualsense_mail;
static SyncStepClock dualsense_clock;
static unsigned dualsense_counter;
static HANDLE dualsense_file=INVALID_HANDLE_VALUE,dualsense_mapping;
static unsigned char *dualsense_view;
static int dualsense_failed,dualsense_written,dualsense_was_valid;
static DWORD dualsense_last_write;
static HANDLE dualsense_owner_file=INVALID_HANDLE_VALUE,dualsense_owner_mapping;
static const unsigned char *dualsense_owner_view;
static DWORD dualsense_owner_open_tick;
static int dualsense_owner_attempted;
static DualSenseOwner dualsense_owner;
static DualSenseWheels dualsense_wheels;
static int readable(const void *p,size_t size);
static void *current_drive(void);
static int custom_drive(void *drive);
static float engine_peak(void *engine);
static void dualsense_invalidate(void)
{
 EnterCriticalSection(&dualsense_lock);
 dualsense_wheels_reset(&dualsense_wheels);
 dualsense_mail.sample=(DualSenseSample){0};dualsense_mail.kick=(DualSenseKick){0};LeaveCriticalSection(&dualsense_lock);
}
static void dualsense_configure(void)
{
 int enabled=(int)GetPrivateProfileIntA("DualSense","Enabled",0,realism_path)==1;
 EnterCriticalSection(&dualsense_lock);dualsense_mail.enabled=enabled;LeaveCriticalSection(&dualsense_lock);
}
static void dualsense_capture(void *engine,double dt)
{
 if(!hooks_ready||!realistic)return;
 void *drive=current_drive();
 if(readable(drive,0x24)&&*(void **)((char *)drive+0x20)==engine)sync_step_capture(&dualsense_clock,dt);
}
static void dualsense_publish_drive(void *drive)
{
 if(!realistic||drive!=current_drive())return;
 double dt=sync_step_consume(&dualsense_clock);if(dt==0)return;
 void *self=wheel?*(void **)(wheel+0x61300):NULL;
 if(!readable(drive,0x40)||!readable(self,0x300)||*(unsigned char *)((char *)self+0x2b4)){dualsense_invalidate();return;}
 void *engine=*(void **)((char *)drive+0x20),*car=*(void **)((char *)self+0x2a8);
 if(!readable(engine,0x24)||!readable(car,0x430)){dualsense_invalidate();return;}
 int manual=*(int *)((char *)self+0x2c0)==1;
 if(manual&&input_lost){dualsense_invalidate();return;}
 DualSenseSample s={.tick=GetTickCount(),.valid=1,.gear=*(int *)((char *)drive+0x3c),.counter=++dualsense_counter};
 s.rpm=*(float *)((char *)engine+0x14)*9.549304f;
 void *spec=*(void **)((char *)engine+0x18);
 if(readable(spec,12)){
  float limit=*(float *)((char *)spec+8);
  if(isfinite(limit)&&limit>0&&limit<=100000)s.rpm_limit=limit;
 }
 s.speed=*(float *)((char *)car+0x42c);
 s.throttle=*(float *)((char *)self+0x20c); /* Native processed throttle, not a trigger-axis guess. */
 s.clutch_press=manual?1-*(float *)((char *)self+0x2f4):0;
 s.flags=manual?DS_MANUAL:0;
 if(source_kind==SOURCE_GAMEPAD){
  s.clutch_trigger=pad_clutch==BIND_LT?1:pad_clutch==BIND_RT?2:0;
  s.throttle_trigger=pad_throttle==BIND_LT?1:pad_throttle==BIND_RT?2:0;
 }
 double slip=0,loads[4]={0},slips[4]={0};unsigned contacts[4]={0};
 uintptr_t wheel_ids[4]={0};float wheel_xy[4][2]={{0}};
 int wheels_ok=1,layout_ok=1;
 for(int i=0;i<4;i++){
  void *w=*(void **)((char *)drive+0x24+4*i);
  wheel_ids[i]=(uintptr_t)w;
  if(!readable(w,0x170)){wheels_ok=0;break;}
  contacts[i]=*(unsigned char *)((char *)w+0x11c);
  loads[i]=*(float *)((char *)w+0x164);slips[i]=*(float *)((char *)w+0x16c);
  if(contacts[i]>1||(contacts[i]&&(!isfinite(loads[i])||loads[i]<0||loads[i]>1e8||
     !isfinite(slips[i])||fabs(slips[i])>1e8))){wheels_ok=0;break;}
  if(contacts[i])slip=fmax(slip,fabs(slips[i]));
  void *wheel_spec=*(void **)((char *)w+0x14);
  if(!readable(wheel_spec,8))layout_ok=0;
  else{wheel_xy[i][0]=*(float *)wheel_spec;wheel_xy[i][1]=*(float *)((char *)wheel_spec+4);}
 }
 /* Bad optional wheel measurements do not discard RPM, clutch or profile telemetry. */
 s.slip=wheels_ok?(float)fmin(1,slip/fmax(3,fabs(s.speed))):0;
 EnterCriticalSection(&dualsense_lock);
 if(wheels_ok&&layout_ok&&dualsense_wheel_layout(wheel_xy))
  dualsense_wheel_sample(&dualsense_wheels,&s,(uintptr_t)car,(uintptr_t)drive,
   wheel_ids,loads,slips,contacts,dt);
 else dualsense_wheels_reset(&dualsense_wheels);
 LeaveCriticalSection(&dualsense_lock);
 if(custom_drive(drive)){
  if(motor.stalled)s.flags|=DS_STALLED;
  if(cranking)s.flags|=DS_CRANKING;
  if(synchronizer_model){
   if(!sync_ready||!sync_result.valid){dualsense_invalidate();return;}
   double peak=engine_peak(engine);
   if(!isfinite(peak)||peak<=0){dualsense_invalidate();return;}
   s.load=(float)(fabs(sync_result.clutch_torque)/peak);
   s.mismatch=(float)(sync_state.engine_omega-sync_state.input_omega);
  }
 }
 LONG until=InterlockedCompareExchange(&grind_until,0,0);
 if(until&&(LONG)((DWORD)until-s.tick)>0)s.flags|=DS_GRINDING;
 void *context=*(void **)((char *)car+0x14);
 if(readable(context,0x90c)&&readable(spec,16)){
  s.car_type=*(int *)((char *)context+0x908);
  s.bite_start=bite_start;s.bite_end=bite_end;s.clutch_capacity_factor=clutch_strength;
  s.idle_rpm=*(float *)spec;s.engine_inertia=*(float *)((char *)spec+0xc);
  s.engine_peak=engine_peak(engine);s.shift_light_rpm=s.rpm_limit;
  if(s.gear>=2&&s.gear<=7&&readable(drive,0xf4)){
   int performance_class=*(int *)((char *)drive+0xf0);
   if(performance_class>=0&&performance_class<=3){
    double target=*(float *)((char *)drive+0x84+4*s.gear);
    double weight=*(float *)(0x7fbf60+4*(performance_class*9+s.gear));
    s.shift_light_rpm=(float)car_native_shift_light_rpm(s.gear,performance_class,target,weight,
     *(float *)((char *)drive+0xe4),*(float *)0x7fbf5c,s.rpm_limit);
   }
  }
  s.profile_valid=1;
 }
 EnterCriticalSection(&dualsense_lock);
 s.kick=(float)dualsense_kick(&dualsense_mail.kick,s.load,dt);dualsense_mail.sample=s;
 LeaveCriticalSection(&dualsense_lock);
}
/* File/mapping operations run only on the existing input timer, never a force hook. */
static int dualsense_open(void)
{
 char path[MAX_PATH];if(!runtime_path("NFSU2_TELEMETRY_PATH",path))return 0;
 dualsense_file=CreateFileA(path,GENERIC_READ|GENERIC_WRITE,
  FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
 if(dualsense_file==INVALID_HANDLE_VALUE)return 0;
 DWORD size=GetFileSize(dualsense_file,NULL);
 if(size==0){if(SetFilePointer(dualsense_file,DUALSENSE_BYTES,NULL,FILE_BEGIN)==INVALID_SET_FILE_POINTER||!SetEndOfFile(dualsense_file))return 0;}
 else if(size!=DUALSENSE_BYTES){SetLastError(ERROR_INVALID_DATA);return 0;}
 dualsense_mapping=CreateFileMappingA(dualsense_file,NULL,PAGE_READWRITE,0,DUALSENSE_BYTES,NULL);
 if(!dualsense_mapping)return 0;
 dualsense_view=MapViewOfFile(dualsense_mapping,FILE_MAP_WRITE|FILE_MAP_READ,0,0,DUALSENSE_BYTES);
 if(!dualsense_view)return 0;
 uint32_t magic=dualsense_u32(dualsense_view);
 if((magic&&magic!=DUALSENSE_MAGIC)||(magic&&dualsense_u32(dualsense_view+4)!=1)){SetLastError(ERROR_INVALID_DATA);return 0;}
 log_status("dualsense_publisher_ready=1 protocol=NSD5 version=1 bytes=128 rate_hz=25 wheel_loads=relative_baseline_half impact=unknown_zero wheel_order=FL_FR_RL_RR");return 1;
}
static void dualsense_write(const unsigned char packet[DUALSENSE_BYTES])
{
 volatile LONG *sequence=(volatile LONG *)(dualsense_view+8);
 uint32_t previous=(uint32_t)InterlockedCompareExchange(sequence,0,0),odd=(previous+1)|1u;
 InterlockedExchange(sequence,(LONG)odd);MemoryBarrier();
 memcpy(dualsense_view,packet,8);memcpy(dualsense_view+12,packet+12,DUALSENSE_BYTES-12);
 MemoryBarrier();InterlockedExchange(sequence,(LONG)(odd+1));
}
static void dualsense_close(void)
{
 if(dualsense_view)UnmapViewOfFile(dualsense_view);
 if(dualsense_mapping)CloseHandle(dualsense_mapping);
 if(dualsense_file!=INVALID_HANDLE_VALUE)CloseHandle(dualsense_file);
 dualsense_view=NULL;dualsense_mapping=NULL;dualsense_file=INVALID_HANDLE_VALUE;
}
static void dualsense_tick(int focused)
{
 int enabled;DualSenseSample sample;unsigned char packet[DUALSENSE_BYTES];
 EnterCriticalSection(&dualsense_lock);enabled=dualsense_mail.enabled;sample=dualsense_mail.sample;LeaveCriticalSection(&dualsense_lock);
 /* WM_TIMER carries queue time; read the clock after the physics snapshot. */
 DWORD now=GetTickCount();
 if(dualsense_failed||(!enabled&&!dualsense_view))return;
 int valid=dualsense_frame(packet,sample,enabled,focused,now);
 if(dualsense_written&&now-dualsense_last_write<40&&!(dualsense_was_valid&&!valid))return;
 if(!dualsense_view&&!dualsense_open()){
  DWORD error=GetLastError();dualsense_failed=1;dualsense_close();
  char message[112];snprintf(message,sizeof(message),"dualsense_publisher_disabled=1 error=%lu physics_unchanged=1",(unsigned long)error);log_status(message);return;
 }
 dualsense_write(packet);dualsense_last_write=now;dualsense_written=1;dualsense_was_valid=valid;
}
static void dualsense_owner_close(void)
{
 if(dualsense_owner_view)UnmapViewOfFile(dualsense_owner_view);
 if(dualsense_owner_mapping)CloseHandle(dualsense_owner_mapping);
 if(dualsense_owner_file!=INVALID_HANDLE_VALUE)CloseHandle(dualsense_owner_file);
 dualsense_owner_view=NULL;dualsense_owner_mapping=NULL;dualsense_owner_file=INVALID_HANDLE_VALUE;dualsense_owner=(DualSenseOwner){0};
}
static int dualsense_owns_feedback(DWORD now)
{
 EnterCriticalSection(&dualsense_lock);int enabled=dualsense_mail.enabled;LeaveCriticalSection(&dualsense_lock);
 if(!enabled)return 0;
 if(dualsense_owner_view&&dualsense_owner.initialized&&now-dualsense_owner.tick>250&&now-dualsense_owner_open_tick>=1000)dualsense_owner_close();
 if(!dualsense_owner_view){
  if(dualsense_owner_attempted&&now-dualsense_owner_open_tick<1000)return 0;
  dualsense_owner_attempted=1;dualsense_owner_open_tick=now;
  char path[MAX_PATH];if(!runtime_path("NFSU2_GYRO_PATH",path))return 0;
  dualsense_owner_file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
  if(dualsense_owner_file==INVALID_HANDLE_VALUE)return 0;
  if(GetFileSize(dualsense_owner_file,NULL)!=32){dualsense_owner_close();return 0;}
  dualsense_owner_mapping=CreateFileMappingA(dualsense_owner_file,NULL,PAGE_READONLY,0,32,NULL);
  if(dualsense_owner_mapping)dualsense_owner_view=MapViewOfFile(dualsense_owner_mapping,FILE_MAP_READ,0,0,32);
  if(!dualsense_owner_view){dualsense_owner_close();return 0;}
 }
 unsigned char packet[32];uint32_t first=*(const volatile uint32_t *)dualsense_owner_view;
 if(first&1)return 0;
 MemoryBarrier();memcpy(packet,dualsense_owner_view,32);MemoryBarrier();
 if(first!=*(const volatile uint32_t *)dualsense_owner_view||first!=dualsense_u32(packet))return 0;
 FILETIME time;ULARGE_INTEGER stamp;GetSystemTimeAsFileTime(&time);stamp.LowPart=time.dwLowDateTime;stamp.HighPart=time.dwHighDateTime;
 return dualsense_owner_active(&dualsense_owner,packet,stamp.QuadPart/10000-11644473600000ULL,now);
}
static void dualsense_shutdown(void)
{
 if(dualsense_view){unsigned char packet[DUALSENSE_BYTES];dualsense_frame(packet,(DualSenseSample){0},0,0,GetTickCount());dualsense_write(packet);}
 dualsense_close();dualsense_owner_close();
}
#endif
