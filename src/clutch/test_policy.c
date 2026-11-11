// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include "clutch-policy.h"
#include "manual_core.h"
#include <assert.h>
#include <stdio.h>
/* Native H gate fixture from Wheel Fix d299..d2f7/d728. MapClutch=0 closed,
   1 open. Policy1 waits; policy2 waits for its timer. The -100 sentinel skips gating. */
static int native_wait_fixture(int previous,int selected,int clutch,int pending,int policy,unsigned elapsed,unsigned delay)
{
 if(!policy||clutch<0)return 0;
 if(previous==-100)return 0;
 if(previous!=selected)pending=selected!=-2&&clutch==0;
 if(pending&&(clutch==1||(policy==2&&elapsed>=delay)))pending=0;
 return pending;
}
int main(void)
{
 /* Native rest must not freeze released neutral, including an already sleeping car. */
 for(int rest=1;rest<=2;rest++) {
  assert(clutch_neutral_coast(rest,1,1,1,0,0));
  assert(clutch_neutral_coast(rest,1,1,1,1.0f/255.0f,0));
  assert(!clutch_neutral_coast(rest,0,1,1,0,0));
  for(int gear=0;gear<8;gear++)if(gear!=1) {
   assert(!clutch_neutral_coast(rest,1,gear,1,0,0));
   assert(!clutch_neutral_coast(rest,1,1,gear,0,0));
  }
  const float blocked[]={2.0f/255.0f,1,-1,NAN,INFINITY};
  for(unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);i++) {
   assert(!clutch_neutral_coast(rest,1,1,1,blocked[i],0));
   assert(!clutch_neutral_coast(rest,1,1,1,0,blocked[i]));
  }
 }
 assert(!clutch_neutral_coast(0,1,1,1,0,0));
 assert(!clutch_neutral_coast(3,1,1,1,0,0));
 assert(clutch_physics_eligible(1,1,0));
 assert(!clutch_physics_eligible(0,1,0));
 for(int mode=0;mode<=4;mode++) {
  assert(!clutch_physics_eligible(1,mode,1));
  assert(clutch_physics_eligible(1,mode,0)==(mode==1));
 }
 /* Countdown retains native gear ownership; GO takes over once, never each frame. */
 int race_owned=0;
 for(int frame=0;frame<180;frame++) {
  int hold=frame<120;
  int eligible=clutch_physics_eligible(1,1,hold);
  assert(clutch_proxy_action(&race_owned,eligible)==(hold?CLUTCH_PROXY_NATIVE:CLUTCH_PROXY_SKIP));
  assert(race_owned==!hold);
 }
 assert(clutch_proxy_action(&race_owned,clutch_physics_eligible(1,1,1))==CLUTCH_PROXY_REARM);
 assert(clutch_proxy_action(&race_owned,clutch_physics_eligible(1,1,1))==CLUTCH_PROXY_NATIVE);
 /* H handback uses neutral, not startup: stale pending cannot affect a new request. */
 assert(native_wait_fixture(-100,3,0,0,1,0,100)==0);
 assert(native_wait_fixture(-2,3,0,0,1,0,100)==1);
 assert(native_wait_fixture(-2,3,1,0,1,0,100)==0);
 assert(native_wait_fixture(-2,-2,0,0,1,0,100)==0);
 assert(native_wait_fixture(3,3,0,1,2,99,100)==1);
 assert(native_wait_fixture(3,3,0,1,2,100,100)==0);
 assert(native_wait_fixture(-2,3,0,0,0,0,100)==0);
 int owned=0;
 assert(clutch_proxy_action(&owned,0)==CLUTCH_PROXY_NATIVE);
 assert(clutch_proxy_action(&owned,1)==CLUTCH_PROXY_SKIP&&owned);
 for(int i=0;i<100;i++)assert(clutch_proxy_action(&owned,1)==CLUTCH_PROXY_SKIP);
 assert(clutch_proxy_action(&owned,0)==CLUTCH_PROXY_REARM&&!owned);
 assert(clutch_proxy_action(&owned,0)==CLUTCH_PROXY_NATIVE);
 assert(clutch_proxy_action(&owned,1)==CLUTCH_PROXY_SKIP);
 assert(clutch_proxy_action(&owned,0)==CLUTCH_PROXY_REARM);

 double ratios[8]={-3.2,0,3.5,2.1,1.4,1,.8,.65};unsigned mask=clutch_gear_mask(ratios);
 for(int i=0;i<8;i++){ClutchTarget a=clutch_choose_target(i,2,mask);assert(a.target==i&&!a.reason);}
 ClutchTarget a=clutch_choose_target(8,2,mask);assert(a.target==2&&a.reason==1);
 a=clutch_choose_target(-1,1,mask);assert(a.target==1&&a.reason==1);
 ratios[7]=0;mask=clutch_gear_mask(ratios);a=clutch_choose_target(7,6,mask);assert(a.target==6&&a.reason==2);
 ratios[4]=0;mask=clutch_gear_mask(ratios);a=clutch_choose_target(4,1,mask);assert(a.target==1&&a.reason==2);
 a=clutch_choose_target(4,9,mask);assert(a.target==1&&a.reason==2);
 ManualParams p={.engine_inertia=.2,.input_inertia=.02,.output_inertia=10,.clutch_capacity=250,.synch_capacity=8,.match_rad_s=.5,.max_engagement_load=3,.grind_delay=.25,.reverse_max_mps=.3};
 for(int i=0;i<8;i++)p.ratios[i]=ratios[i];
 ManualState s={.engine_omega=400,.input_omega=123,.output_omega=123/3.5,.mode=MANUAL_ENGAGED,.gear=1,.target=1};
 a=clutch_choose_target(7,2,mask);ManualOutput o=manual_step(&s,&p,(ManualInput){a.target-1,0,0,.01});
 assert(o.valid&&s.mode==MANUAL_ENGAGED&&s.gear==1&&fabs(s.input_omega-123)<1e-10);
 s=(ManualState){.engine_omega=400,.input_omega=123,.mode=MANUAL_SYNCHRONIZING,.target=2,.synch_time=.1};
 a=clutch_choose_target(7,1,mask);o=manual_step(&s,&p,(ManualInput){a.target-1,0,0,.01});
 assert(o.valid&&s.mode==MANUAL_NEUTRAL&&s.input_omega==123); /* cancel request, preserve rotating shaft */
 ClutchEstimates d=clutch_estimate_defaults();assert(clutch_estimates_equal(d,clutch_estimates_validate(d)));
 ClutchEstimates bad={0,101,-1,101,2001,0};assert(clutch_estimates_equal(d,clutch_estimates_validate(bad)));
 ClutchEstimates lo={1,1,1,1,20,1},hi={100,100,500,100,2000,100};
 assert(clutch_estimates_equal(lo,clutch_estimates_validate(lo)));assert(clutch_estimates_equal(hi,clutch_estimates_validate(hi)));
 bad=d;bad.match_rpm=100;assert(!clutch_estimates_equal(d,clutch_estimates_validate(bad)));
 puts("PASS gear mask/rejection/state-preservation and bounded estimate settings");
}
