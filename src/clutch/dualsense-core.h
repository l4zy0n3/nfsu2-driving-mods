// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_DUALSENSE_CORE_H
#define U2_DUALSENSE_CORE_H
#include <math.h>
#include <stdint.h>
#include <string.h>
#define DUALSENSE_BYTES 128
#define DUALSENSE_MAGIC 0x3544534eu
enum {DS_VALID=1,DS_FOCUSED=2,DS_STALLED=4,DS_CRANKING=8,DS_GRINDING=16,DS_MANUAL=32};
enum {DS_EXT_PROFILE=1,DS_EXT_WHEELS=2};
typedef struct {
 uint32_t tick,flags,counter;int valid,gear;
 unsigned clutch_trigger,throttle_trigger; /* 0=none, 1=left, 2=right. */
 float rpm,rpm_limit,clutch_press,throttle,speed,slip,load,mismatch,kick;
 int profile_valid,car_type;
 float bite_start,bite_end,idle_rpm,engine_inertia,clutch_capacity_factor,shift_light_rpm,engine_peak;
 int wheel_valid;
 float wheel_load[4],slip_left,slip_right; /* Protocol order FL,FR,RL,RR; relative, not Newtons. */
} DualSenseSample;
typedef struct {double previous,kick;} DualSenseKick;
typedef struct {uint32_t sequence,tick;int initialized;} DualSenseOwner;
typedef struct {
 uintptr_t car,drive,wheels[4];uint32_t tick,baseline_tick;int ready;
 double baseline[4]; /* Measured native-order loads, held while driving. */
} DualSenseWheels;
static uint32_t dualsense_u32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void dualsense_put_u32(unsigned char *p,uint32_t value)
{for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(value>>(i*8));}
static void dualsense_put_float(unsigned char *p,float value)
{uint32_t bits;memcpy(&bits,&value,4);dualsense_put_u32(p,bits);}
static float dualsense_unit(float value){return fmaxf(0,fminf(1,value));}
static void dualsense_wheels_reset(DualSenseWheels *state)
{memset(state,0,sizeof(*state));}
/* Native order FL,FR,RR,RL is asset-proven; validate that geometry before using it. */
static int dualsense_wheel_layout(const float xy[4][2])
{
 for(int i=0;i<4;i++)for(int j=0;j<2;j++)if(!isfinite(xy[i][j])||fabsf(xy[i][j])>100)return 0;
 return xy[0][1]>0&&xy[3][1]>0&&xy[1][1]<0&&xy[2][1]<0&&
  fminf(xy[0][0],xy[1][0])>fmaxf(xy[2][0],xy[3][0]);
}
/* New baselines settle100ms, covering two standard40ms mailbox periods.
   Invalid/gapped/new owners warm up again.
   A baseline maps to .5; twice that measured load saturates at1. No force-unit claim. */
static int dualsense_wheel_sample(DualSenseWheels *state,DualSenseSample *s,
 uintptr_t car,uintptr_t drive,const uintptr_t wheels[4],const double load[4],
 const double slip[4],const unsigned contact[4],double dt)
{
 s->wheel_valid=0;memset(s->wheel_load,0,sizeof(s->wheel_load));s->slip_left=s->slip_right=0;
 if(!car||!drive||!isfinite(s->speed)||!isfinite(dt)||dt<=0||dt>.1)goto invalid;
 int all_contact=1;
 for(int i=0;i<4;i++){
  if(!wheels[i]||contact[i]>1)goto invalid;
  if(!contact[i]){all_contact=0;continue;} /* Unloaded stale native floats are irrelevant. */
  if(!isfinite(load[i])||load[i]<0||load[i]>1e8||!isfinite(slip[i])||fabs(slip[i])>1e8)goto invalid;
  if(load[i]<=0)all_contact=0;
 }
 int changed=state->car!=car||state->drive!=drive||s->tick-state->tick>250;
 for(int i=0;i<4;i++)changed|=state->wheels[i]!=wheels[i];
 if(changed)dualsense_wheels_reset(state);
 state->car=car;state->drive=drive;state->tick=s->tick;
 memcpy(state->wheels,wheels,sizeof(state->wheels));
 if(!state->ready){
  if(all_contact){memcpy(state->baseline,load,sizeof(state->baseline));state->ready=1;state->baseline_tick=s->tick;}
  return 0;
 }
 if(s->tick-state->baseline_tick<100)return 0;
 double normalized[4],side[2]={0,0},denominator=fmax(3,fabs(s->speed));
 for(int i=0;i<4;i++){
  if(!isfinite(state->baseline[i])||state->baseline[i]<=0)goto invalid;
  normalized[i]=contact[i]?fmin(1,load[i]/(2*state->baseline[i])):0;
  if(contact[i]){
   int right=i==1||i==2;
   side[right]=fmax(side[right],fmin(1,fabs(slip[i])/denominator));
  }
 }
 /* Follow slow stationary weight changes, never track away a driving bump. */
 if(all_contact&&fabs(s->speed)<.1)for(int i=0;i<4;i++)
  state->baseline[i]+=(load[i]-state->baseline[i])*(dt/(2+dt));
 const unsigned order[4]={0,1,3,2};
 for(int i=0;i<4;i++)s->wheel_load[i]=(float)normalized[order[i]];
 s->slip_left=(float)side[0];s->slip_right=(float)side[1];s->wheel_valid=1;return 1;
invalid:
 dualsense_wheels_reset(state);return 0;
}
static int dualsense_wheel_extension_valid(DualSenseSample s)
{
 if(!s.wheel_valid||!isfinite(s.slip_left)||!isfinite(s.slip_right)||
  s.slip_left<0||s.slip_left>1||s.slip_right<0||s.slip_right>1)return 0;
 for(int i=0;i<4;i++)if(!isfinite(s.wheel_load[i])||s.wheel_load[i]<0||s.wheel_load[i]>1)return 0;
 return 1;
}
static int dualsense_profile_valid(DualSenseSample s)
{
 const float fields[]={s.bite_start,s.bite_end,s.idle_rpm,s.engine_inertia,s.clutch_capacity_factor,s.shift_light_rpm,s.engine_peak};
 if(!s.profile_valid||s.car_type<0||s.car_type>255)return 0;
 for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++)if(!isfinite(fields[i]))return 0;
 return s.bite_start>=0&&s.bite_start<s.bite_end&&s.bite_end<=1&&
  s.idle_rpm>0&&s.idle_rpm<=s.rpm_limit&&s.engine_inertia>0&&s.engine_inertia<=1e8&&
  s.clutch_capacity_factor>=.5f&&s.clutch_capacity_factor<=3&&s.shift_light_rpm>0&&s.shift_light_rpm<=100000&&s.engine_peak>0&&s.engine_peak<=1e8;
}
static double dualsense_kick(DualSenseKick *state,double load,double dt)
{
 if(!isfinite(load)||load<0||!isfinite(dt)||dt<=0||dt>.1){*state=(DualSenseKick){0};return 0;}
 load=fmax(0,fmin(1,load));
 state->kick=fmax(state->kick*exp(-dt/.08),fmax(0,load-state->previous));
 state->previous=load;return state->kick;
}
/* Zero inactive payloads; the header heartbeat remains readable while paused. */
static int dualsense_frame(unsigned char out[DUALSENSE_BYTES],DualSenseSample s,int enabled,int focused,uint32_t now)
{
 _Static_assert(sizeof(float)==4,"Protocol requires IEEE754 float32");
 memset(out,0,DUALSENSE_BYTES);
 dualsense_put_u32(out,DUALSENSE_MAGIC);dualsense_put_u32(out+4,1);dualsense_put_u32(out+12,now);
 const float fields[]={s.rpm,s.rpm_limit,s.clutch_press,s.throttle,s.speed,s.slip,s.load,s.mismatch,s.kick};
 if(!enabled||!focused||!s.valid||now-s.tick>100||s.gear<0||s.gear>8)return 0;
 for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++)if(!isfinite(fields[i]))return 0;
 if(s.rpm<0||s.rpm>100000||s.rpm_limit<0||s.rpm_limit>100000||fabsf(s.speed)>100000||fabsf(s.mismatch)>1e7)return 0;
 uint32_t flags=DS_VALID|DS_FOCUSED|(s.flags&(DS_STALLED|DS_CRANKING|DS_GRINDING|DS_MANUAL));
 if(s.clutch_trigger<=2&&s.throttle_trigger<=2&&(!s.clutch_trigger||s.clutch_trigger!=s.throttle_trigger))
  flags|=(s.clutch_trigger<<6)|(s.throttle_trigger<<8);
 dualsense_put_u32(out+16,flags);dualsense_put_u32(out+20,(uint32_t)s.gear);
 dualsense_put_float(out+24,s.rpm);dualsense_put_float(out+28,s.rpm_limit);
 dualsense_put_float(out+32,dualsense_unit(s.clutch_press));dualsense_put_float(out+36,dualsense_unit(s.throttle));
 dualsense_put_float(out+40,s.speed);dualsense_put_float(out+44,dualsense_unit(s.slip));
 dualsense_put_float(out+48,dualsense_unit(s.load));dualsense_put_float(out+52,s.mismatch);
 dualsense_put_float(out+56,dualsense_unit(s.kick));
 /* Impact76 stays zero. Wheel values below are relative measurements, not Newtons. */
 dualsense_put_u32(out+80,s.counter);
 /* Independent optional v1 extensions. Marker0 preserves old producers. */
 uint32_t extensions=0;
 if(dualsense_profile_valid(s)){
  dualsense_put_u32(out+84,(uint32_t)s.car_type);
  dualsense_put_float(out+88,s.bite_start);dualsense_put_float(out+92,s.bite_end);
  dualsense_put_float(out+96,s.idle_rpm);dualsense_put_float(out+100,s.engine_inertia);
  dualsense_put_float(out+104,s.clutch_capacity_factor);dualsense_put_float(out+108,s.shift_light_rpm);
  dualsense_put_float(out+112,s.engine_peak);extensions|=DS_EXT_PROFILE;
 }
 if(dualsense_wheel_extension_valid(s)){
  for(unsigned i=0;i<4;i++)dualsense_put_float(out+60+4*i,s.wheel_load[i]);
  dualsense_put_float(out+120,s.slip_left);dualsense_put_float(out+124,s.slip_right);
  extensions|=DS_EXT_WHEELS;
 }
 dualsense_put_u32(out+116,extensions);
 return 1;
}
static int dualsense_owner_active(DualSenseOwner *state,const unsigned char packet[32],uint64_t unix_ms,uint32_t now)
{
 uint32_t seq=dualsense_u32(packet),flags=dualsense_u32(packet+12);
 uint64_t stamp=(uint64_t)dualsense_u32(packet+16)|(uint64_t)dualsense_u32(packet+20)<<32;
 if((seq&1)||dualsense_u32(packet+4)!=0x3247594eu||dualsense_u32(packet+8)!=1||stamp>unix_ms||unix_ms-stamp>250)return 0;
 if(!state->initialized||state->sequence!=seq){state->initialized=1;state->sequence=seq;state->tick=now;}
 return (flags&2)!=0&&now-state->tick<=250;
}
#endif
