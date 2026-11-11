// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_RUMBLE_NATIVE_H
#define U2_RUMBLE_NATIVE_H
#include "rumble-core.h"
typedef DWORD (WINAPI *PadVibrateFn)(DWORD,XINPUT_VIBRATION *);
static PadVibrateFn pad_vibrate;
static CRITICAL_SECTION rumble_lock;
static struct {int enabled,gain;RumbleMotion motion;RumbleSample sample;} rumble_mail={.enabled=1,.gain=35};
static RumbleSchedule rumble_schedule;
static DWORD rumble_index=XUSER_MAX_COUNT;
static SyncStepClock rumble_clock;
static int rumble_reported,rumble_failed;
static int readable(const void *p,size_t size);
static void *current_drive(void);
static int custom_drive(void *drive);
static float engine_peak(void *engine);
static void rumble_invalidate(void)
{
 EnterCriticalSection(&rumble_lock);
 rumble_mail.motion=(RumbleMotion){0};rumble_mail.sample=(RumbleSample){0};
 LeaveCriticalSection(&rumble_lock);
}
static void rumble_configure(void)
{
 int enabled=(int)GetPrivateProfileIntA("Rumble","Enabled",1,realism_path);
 int gain=(int)GetPrivateProfileIntA("Rumble","GainPercent",35,realism_path);
 if((enabled!=0&&enabled!=1)||gain<0||gain>50){enabled=0;gain=35;}
 EnterCriticalSection(&rumble_lock);rumble_mail.enabled=enabled;rumble_mail.gain=gain;LeaveCriticalSection(&rumble_lock);
}
static void rumble_capture(void *engine,double dt)
{
 if(source_kind!=SOURCE_GAMEPAD||!hooks_ready||!realistic)return;
 void *drive=current_drive();
 if(readable(drive,0x24)&&*(void **)((char *)drive+0x20)==engine)sync_step_capture(&rumble_clock,dt);
}
static void rumble_publish_drive(void *drive)
{
 if(source_kind!=SOURCE_GAMEPAD||!realistic||drive!=current_drive())return;
 double dt=sync_step_consume(&rumble_clock);if(dt==0)return;
 void *self=wheel?*(void **)(wheel+0x61300):NULL;
 if(!readable(drive,0x34)||
    !readable(self,0x2c4)||*(unsigned char *)((char *)self+0x2b4)){rumble_invalidate();return;}
 void *car=*(void **)((char *)self+0x2a8);
 if(!readable(car,0x430)){rumble_invalidate();return;}
 double slip=0,speed=*(float *)((char *)car+0x42c),load=0;
 for(int i=0;i<4;i++){
  void *w=*(void **)((char *)drive+0x24+4*i);
  if(!readable(w,0x170)){rumble_invalidate();return;}
  /* PrepareForces copies longitudinal slip speed here; +0x118 is cleared after integration. */
  double value=*(float *)((char *)w+0x16c);
  if(!isfinite(value)){rumble_invalidate();return;}
  if(*(unsigned char *)((char *)w+0x11c))slip=fmax(slip,fabs(value));
 }
 if(custom_drive(drive)&&synchronizer_model){
  if(!sync_ready||!sync_result.valid){rumble_invalidate();return;}
  double peak=engine_peak(*(void **)((char *)drive+0x20));
  if(!isfinite(peak)||peak<=0){rumble_invalidate();return;}
  load=fabs(sync_result.clutch_torque)/peak;
 }
 DWORD now=GetTickCount();LONG until=InterlockedCompareExchange(&grind_until,0,0);
 EnterCriticalSection(&rumble_lock);
 rumble_mail.sample=rumble_motion(&rumble_mail.motion,slip,speed,load,until&&(LONG)((DWORD)until-now)>0,dt,now);
 LeaveCriticalSection(&rumble_lock);
}
/* Only the existing input timer owns normal XInput motor commands. */
static void rumble_stop(void)
{
 if(pad_vibrate&&rumble_index<XUSER_MAX_COUNT){XINPUT_VIBRATION zero={0};pad_vibrate(rumble_index,&zero);}
 rumble_index=XUSER_MAX_COUNT;rumble_schedule=(RumbleSchedule){0};
}
static void rumble_tick(int focused,DWORD now)
{
 if(dualsense_owns_feedback(now)){rumble_stop();return;}
 if(!pad_state||!pad_vibrate||source_kind!=SOURCE_GAMEPAD){rumble_stop();return;}
 DWORD index=XUSER_MAX_COUNT;XINPUT_STATE state;
 if(pad_state(gamepad_index,&state)==ERROR_SUCCESS)index=gamepad_index;
 if(index!=rumble_index){rumble_stop();rumble_invalidate();rumble_index=index;}
 if(index==XUSER_MAX_COUNT)return;
 EnterCriticalSection(&rumble_lock);
 RumbleLevels levels=rumble_levels(rumble_mail.sample,rumble_mail.enabled,rumble_mail.gain,1,focused,now);
 LeaveCriticalSection(&rumble_lock);
 if(!rumble_due(&rumble_schedule,levels,now))return;
 XINPUT_VIBRATION motors={levels.left,levels.right};
 DWORD result=pad_vibrate(index,&motors);
 if(result!=ERROR_SUCCESS){rumble_stop();if(!rumble_failed){rumble_failed=1;log_status("xbox_rumble_failed=1 zero_command_attempted=1");}}
 else if(!rumble_reported&&(levels.left||levels.right)){
  rumble_reported=1;log_status("xbox_rumble_active=1 normalized_cues=slip_clutch_grind rate_hz=25 max_gain_pct=50");
 }
}
#endif
