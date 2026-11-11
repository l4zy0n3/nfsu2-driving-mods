// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include "dualsense-core.h"
#include <assert.h>
#include <stdio.h>
static float value(const unsigned char *p)
{uint32_t bits=dualsense_u32(p);float result;memcpy(&result,&bits,4);return result;}
static void inactive(const unsigned char *packet,uint32_t now)
{
 assert(dualsense_u32(packet)==DUALSENSE_MAGIC&&dualsense_u32(packet+4)==1);
 assert(dualsense_u32(packet+12)==now);
 for(unsigned i=16;i<DUALSENSE_BYTES;i++)assert(packet[i]==0);
}
static void owner_packet(unsigned char packet[32],uint32_t sequence,uint32_t flags,uint64_t stamp)
{
 memset(packet,0,32);dualsense_put_u32(packet,sequence);
 dualsense_put_u32(packet+4,0x3247594e);dualsense_put_u32(packet+8,1);dualsense_put_u32(packet+12,flags);
 dualsense_put_u32(packet+16,(uint32_t)stamp);dualsense_put_u32(packet+20,(uint32_t)(stamp>>32));
}
static void wheel_tests(DualSenseSample base)
{
 const float geometry[4][2]={{1.3f,.74f},{1.3f,-.74f},{-1.3f,-.74f},{-1.3f,.74f}};
 assert(dualsense_wheel_layout(geometry));
 float bad_geometry[4][2];memcpy(bad_geometry,geometry,sizeof(geometry));
 bad_geometry[2][1]=.74f;assert(!dualsense_wheel_layout(bad_geometry));
 memcpy(bad_geometry,geometry,sizeof(geometry));bad_geometry[0][0]=-2;
 assert(!dualsense_wheel_layout(bad_geometry));bad_geometry[0][0]=NAN;
 assert(!dualsense_wheel_layout(bad_geometry));
 DualSenseWheels state={0};uintptr_t wheels[4]={100,200,300,400};
 double loads[4]={10,20,30,40},slips[4]={1,2,3,4};unsigned contact[4]={1,1,1,1};
 DualSenseSample s=base;s.tick=1000;s.speed=10;s.profile_valid=1;
 assert(!dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01));
 assert(!s.wheel_valid&&state.ready); /* New car starts with an invalid warm-up frame. */
 s.tick=1099;
 assert(!dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01));
 s.tick=1100;
 assert(dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01));
 for(int i=0;i<4;i++)assert(s.wheel_load[i]==.5f);
 assert(fabsf(s.slip_left-.4f)<1e-6f&&fabsf(s.slip_right-.3f)<1e-6f);
 /* Distinct native loads prove source0,1,3,2 -> protocol FL,FR,RL,RR. */
 loads[0]=5;loads[1]=20;loads[2]=45;loads[3]=80;s.tick++;
 assert(dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01));
 assert(s.wheel_load[0]==.25f&&s.wheel_load[1]==.5f&&s.wheel_load[2]==1&&s.wheel_load[3]==.75f);
 unsigned char packet[DUALSENSE_BYTES];assert(dualsense_frame(packet,s,1,1,s.tick));
 assert(dualsense_u32(packet+116)==3);
 assert(value(packet+60)==.25f&&value(packet+64)==.5f&&value(packet+68)==1&&value(packet+72)==.75f);
 assert(fabsf(value(packet+120)-.4f)<1e-6f&&fabsf(value(packet+124)-.3f)<1e-6f);
 assert(value(packet+76)==0&&dualsense_u32(packet+80)==s.counter);
 s.profile_valid=0;assert(dualsense_frame(packet,s,1,1,s.tick));assert(dualsense_u32(packet+116)==2);
 for(unsigned i=84;i<116;i++)assert(packet[i]==0);
 s.profile_valid=1;
 /* Both left wheels airborne: native stale values, even NaNs, must give zero left output. */
 contact[0]=contact[3]=0;loads[0]=slips[0]=NAN;loads[3]=slips[3]=NAN;s.tick++;
 assert(dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01));
 assert(s.wheel_load[0]==0&&s.wheel_load[2]==0&&s.slip_left==0&&s.slip_right>0);
 contact[1]=contact[2]=0;s.tick++;
 assert(dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01));
 for(int i=0;i<4;i++)assert(s.wheel_load[i]==0);
 assert(s.slip_left==0&&s.slip_right==0);
 contact[0]=contact[1]=contact[2]=contact[3]=1;
 loads[0]=10;loads[3]=40;slips[0]=1;slips[3]=4;
 /* Any invalid contacting wheel resets only the extension; base/profile survives. */
 loads[2]=NAN;s.tick++;
 assert(!dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01)&&!state.ready);
 assert(dualsense_frame(packet,s,1,1,s.tick)&&dualsense_u32(packet+116)==1);
 assert(value(packet+24)==base.rpm&&value(packet+32)==base.clutch_press);
 for(unsigned i=60;i<80;i++)assert(packet[i]==0);
 assert(value(packet+120)==0&&value(packet+124)==0);
 loads[2]=30;
 for(int change=0;change<4;change++){
  uintptr_t car=1,drive=2;
  s.tick++;
  if(change==0)car=3;else if(change==1)drive=4;
  else if(change==2)wheels[2]++;else if(change==3)s.tick+=251;
  assert(!dualsense_wheel_sample(&state,&s,car,drive,wheels,loads,slips,contact,.01));
  s.tick+=100;
  assert(dualsense_wheel_sample(&state,&s,car,drive,wheels,loads,slips,contact,.01));
 }
 dualsense_wheels_reset(&state);s.tick=UINT32_MAX-50;
 assert(!dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01));
 s.tick=48;assert(!dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01)); /*99ms across wrap. */
 s.tick=49;assert(dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01)); /*100ms across wrap. */
 /* Contact data errors and bad step timing also force a fresh baseline. */
 contact[1]=2;s.tick++;assert(!dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01));
 contact[1]=1;assert(!dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.2));
 /* Baseline stays fixed in motion; only slow stationary recalibration is allowed. */
 dualsense_wheels_reset(&state);s=base;s.tick=3000;s.speed=0;
 assert(!dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01));
 double initial=state.baseline[0];loads[0]*=1.5;s.tick+=100;
 assert(dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01));
 assert(s.wheel_load[0]==.75f&&state.baseline[0]>initial&&state.baseline[0]<loads[0]);
 double stationary=state.baseline[0];s.speed=20;s.tick++;
 assert(dualsense_wheel_sample(&state,&s,1,2,wheels,loads,slips,contact,.01));
 assert(state.baseline[0]==stationary);
 /* Encoder rejects malformed wheel metadata independently of profile/base fields. */
 s=base;s.wheel_valid=1;s.wheel_load[0]=NAN;
 assert(dualsense_frame(packet,s,1,1,s.tick)&&dualsense_u32(packet+116)==1);
 s.wheel_load[0]=0;s.slip_left=1.01f;
 assert(dualsense_frame(packet,s,1,1,s.tick)&&dualsense_u32(packet+116)==1);
}
int main(void)
{
 DualSenseSample s={.tick=1000,.flags=DS_STALLED|DS_CRANKING|DS_GRINDING|DS_MANUAL,.counter=123,
  .valid=1,.gear=2,.rpm=4400,.rpm_limit=8500,.clutch_press=.5f,.throttle=.25f,
  .speed=-20,.slip=.75f,.load=.6f,.mismatch=-12,.kick=.4f};
 unsigned char packet[DUALSENSE_BYTES];
 assert(dualsense_frame(packet,s,1,1,1000));
 /* A queued timer stamp can precede the latest physics sample; use a fresh clock. */
 assert(!dualsense_frame(packet,s,1,1,999));
 assert(dualsense_frame(packet,s,1,1,1000));
 assert(packet[0]=='N'&&packet[1]=='S'&&packet[2]=='D'&&packet[3]=='5');
 assert(dualsense_u32(packet+16)==63&&dualsense_u32(packet+20)==2);
 for(unsigned clutch=0;clutch<=3;clutch++)for(unsigned throttle=0;throttle<=3;throttle++){
  s.clutch_trigger=clutch;s.throttle_trigger=throttle;
  assert(dualsense_frame(packet,s,1,1,1000));
  unsigned routes=clutch<=2&&throttle<=2&&(!clutch||clutch!=throttle)?(clutch<<6)|(throttle<<8):0;
  assert(dualsense_u32(packet+16)==(63|routes));
 }
 s.clutch_trigger=s.throttle_trigger=0;
 assert(dualsense_frame(packet,s,1,1,1000));
 assert(value(packet+24)==4400&&value(packet+28)==8500&&value(packet+32)==.5f);
 assert(value(packet+36)==.25f&&value(packet+40)==-20&&value(packet+52)==-12);
 for(unsigned i=60;i<80;i++)assert(packet[i]==0);
 assert(dualsense_u32(packet+80)==123);for(unsigned i=84;i<128;i++)assert(packet[i]==0);
 s.profile_valid=1;s.car_type=27;s.bite_start=.25f;s.bite_end=.78f;s.idle_rpm=850;
 s.engine_inertia=.00026f;s.clutch_capacity_factor=1.45f;s.shift_light_rpm=8000;s.engine_peak=.243f;
 assert(dualsense_frame(packet,s,1,1,1000));
 assert(dualsense_u32(packet+84)==27&&dualsense_u32(packet+116)==1);
 assert(value(packet+88)==.25f&&value(packet+92)==.78f&&value(packet+96)==850);
 assert(value(packet+100)==.00026f&&value(packet+104)==1.45f&&value(packet+108)==8000&&value(packet+112)==.243f);
 for(unsigned i=120;i<128;i++)assert(packet[i]==0);
 DualSenseSample profile=s;
 wheel_tests(profile);
 for(int bad=0;bad<13;bad++){
  s=profile;
  switch(bad){case 0:s.car_type=-1;break;case 1:s.car_type=256;break;
   case 2:s.bite_start=NAN;break;case 3:s.bite_end=s.bite_start;break;
   case 4:s.bite_end=1.01f;break;case 5:s.idle_rpm=0;break;
   case 6:s.engine_inertia=INFINITY;break;case 7:s.engine_inertia=0;break;
   case 8:s.clutch_capacity_factor=.49f;break;case 9:s.clutch_capacity_factor=3.01f;break;
   case 10:s.shift_light_rpm=100001;break;case 11:s.shift_light_rpm=0;break;case 12:s.engine_peak=0;break;}
  assert(dualsense_frame(packet,s,1,1,1000));
  for(unsigned i=84;i<128;i++)assert(packet[i]==0); /* Bad extension never corrupts base telemetry. */
 }
 s=profile;
 s.shift_light_rpm=9000;assert(dualsense_frame(packet,s,1,1,1000));
 assert(value(packet+108)==9000); /* Keep native above-limiter thresholds. */
 assert(!dualsense_frame(packet,s,0,1,1000));inactive(packet,1000);
 s.profile_valid=0;assert(dualsense_frame(packet,s,1,1,1000));
 for(unsigned i=84;i<128;i++)assert(packet[i]==0);
 s.clutch_press=2;s.throttle=-1;s.slip=10;s.load=1.25;s.kick=10;s.flags=0xffffffff;
 assert(dualsense_frame(packet,s,1,1,1000));
 assert(value(packet+32)==1&&value(packet+36)==0&&value(packet+44)==1&&value(packet+48)==1&&value(packet+56)==1);
 assert(dualsense_u32(packet+16)==63);
 assert(!dualsense_frame(packet,s,0,1,1000));inactive(packet,1000);
 assert(!dualsense_frame(packet,s,1,0,1000));inactive(packet,1000);
 assert(!dualsense_frame(packet,s,1,1,1101));inactive(packet,1101);
 s.tick=UINT32_MAX-20;assert(dualsense_frame(packet,s,1,1,20));
 s.rpm=NAN;assert(!dualsense_frame(packet,s,1,1,20));inactive(packet,20);s.rpm=1000;
 s.rpm_limit=0;assert(dualsense_frame(packet,s,1,1,20)&&value(packet+28)==0);
 s.mismatch=INFINITY;assert(!dualsense_frame(packet,s,1,1,20));s.mismatch=0;
 s.gear=-1;assert(!dualsense_frame(packet,s,1,1,20));s.gear=9;assert(!dualsense_frame(packet,s,1,1,20));
 s.gear=8;assert(dualsense_frame(packet,s,1,1,20));
 s.valid=0;assert(!dualsense_frame(packet,s,1,1,20));inactive(packet,20);
 DualSenseKick kick={0};assert(dualsense_kick(&kick,1,.01)==1);
 for(int i=0;i<100;i++)dualsense_kick(&kick,1,.01);
 assert(kick.kick<.00001);assert(dualsense_kick(&kick,NAN,.01)==0&&kick.previous==0);
 DualSenseOwner owner={0};unsigned char other[32];uint64_t epoch=1700000000000ULL;
 owner_packet(other,2,2,epoch);assert(dualsense_owner_active(&owner,other,epoch,1000));
 assert(dualsense_owner_active(&owner,other,epoch+250,1250));
 assert(!dualsense_owner_active(&owner,other,epoch+251,1251));
 owner_packet(other,2,2,epoch+300);assert(!dualsense_owner_active(&owner,other,epoch+300,1300)); /* Frozen sequence cannot renew ownership. */
 owner_packet(other,4,2,epoch+300);assert(dualsense_owner_active(&owner,other,epoch+300,1300));
 owner_packet(other,5,2,epoch+301);assert(!dualsense_owner_active(&owner,other,epoch+301,1301));
 owner_packet(other,6,0,epoch+302);assert(!dualsense_owner_active(&owner,other,epoch+302,1302));
 owner_packet(other,8,2,epoch+303);assert(!dualsense_owner_active(&owner,other,epoch+302,1302));
 owner_packet(other,8,2,epoch+303);other[4]=0;assert(!dualsense_owner_active(&owner,other,epoch+303,1303));
 owner_packet(other,8,2,epoch+303);other[8]=2;assert(!dualsense_owner_active(&owner,other,epoch+303,1303));
 owner=(DualSenseOwner){0};owner_packet(other,10,2,epoch);assert(dualsense_owner_active(&owner,other,epoch,UINT32_MAX-100));
 assert(dualsense_owner_active(&owner,other,epoch+200,99));
 puts("PASS DualSense128byteLE: independent profile/wheel markers, FL/FR/RL/RR order, side slip/contact gates, per-owner baseline/reset/warmup, invalid/stale/focus, clutch pulse and ownership freshness/wrap");
}
