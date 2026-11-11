// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include "manual_core.h"
#include <assert.h>
#include <stdio.h>
static ManualParams params(void)
{
    return (ManualParams){.engine_inertia=.2,.input_inertia=.02,.output_inertia=10,
        .clutch_capacity=250,.synch_capacity=8,.match_rad_s=.5,.max_engagement_load=3,
        .grind_delay=.25,.reverse_max_mps=.3,.ratios={-3.2,0,3.5,2.1,1.4,1,.8,.65}};
}
static double energy(ManualState s,ManualParams p)
{return .5*(p.engine_inertia*s.engine_omega*s.engine_omega+p.input_inertia*s.input_omega*s.input_omega+p.output_inertia*s.output_omega*s.output_omega);}
static void near(double a,double b){assert(fabs(a-b)<1e-7*fmax(1,fmax(fabs(a),fabs(b))));}
int main(void)
{
 ManualParams p=params();unsigned cases=0;
 for(int hz=30;hz<=960;hz*=2)for(int gear=-1;gear<=6;gear++)for(int direction=-1;direction<=1;direction+=2) {
  ManualState s={.engine_omega=direction*300,.input_omega=direction*30,.output_omega=direction*10};
  for(int i=0;i<500;i++){
   ManualState before=s;double e=energy(s,p);double release=(i%100)/99.0;
   ManualOutput o=manual_step(&s,&p,(ManualInput){gear,release,0,1.0/hz});assert(o.valid);
   assert(energy(s,p)<=e+1e-7*fmax(1,e));
   assert(fabs(o.clutch_torque)<=p.clutch_capacity*release*release+1e-8);
   assert(fabs(o.synch_torque)<=p.synch_capacity+1e-8);
   near((s.engine_omega-before.engine_omega)*p.engine_inertia,o.engine_reaction/hz);
   near((s.input_omega-before.input_omega)*p.input_inertia,o.input_reaction/hz);
   near((s.output_omega-before.output_omega)*p.output_inertia,o.output_reaction/hz);cases++;
  }
 }
 /* Engaged steady load and acceleration must not depend on the free shaft inertia. */
 for(int hz=30;hz<=960;hz*=2)for(int gear=-1;gear<=6;gear++)if(gear) {
  double r=p.ratios[gear+1],dt=1.0/hz;
  for(int tiny=0;tiny<2;tiny++) {
   ManualParams q=p;q.input_inertia=tiny?1e-6:p.input_inertia;
   ManualState loaded={.engine_omega=100,.input_omega=100,.output_omega=100/r,.mode=MANUAL_ENGAGED,.gear=gear,.target=gear};
   for(int i=0;i<hz*2;i++) {
    loaded.engine_omega+=100/q.engine_inertia*dt;
    loaded.output_omega-=r*100/q.output_inertia*dt;
    ManualOutput o=manual_step(&loaded,&q,(ManualInput){gear,1,0,dt});assert(o.valid);
    near(o.clutch_torque,100);near(o.output_reaction,r*100);
    near(loaded.engine_omega,100);near(loaded.input_omega,100);near(loaded.output_omega,100/r);
   }
   ManualState moving={.engine_omega=100,.input_omega=100,.output_omega=100/r,.mode=MANUAL_ENGAGED,.gear=gear,.target=gear};
   double connected=q.input_inertia+q.output_inertia/(r*r);
   double alpha=100/(q.engine_inertia+connected);
   for(int i=0;i<hz;i++) {
    moving.engine_omega+=100/q.engine_inertia*dt;
    ManualOutput o=manual_step(&moving,&q,(ManualInput){gear,1,0,dt});assert(o.valid);
    near(o.clutch_torque,100*connected/(q.engine_inertia+connected));
    near(moving.engine_omega,moving.input_omega);near(moving.input_omega,r*moving.output_omega);
   }
   near(moving.engine_omega,100+alpha);
  }
  ManualState inconsistent={.engine_omega=-200,.input_omega=500,.output_omega=-10,.mode=MANUAL_ENGAGED,.gear=gear,.target=gear};
  double before=energy(inconsistent,p);
  ManualOutput o=manual_step(&inconsistent,&p,(ManualInput){gear,1,0,dt});assert(o.valid);
  assert(energy(inconsistent,p)<=before);near(inconsistent.input_omega,r*inconsistent.output_omega);
 }
 ManualParams five=p;five.ratios[7]=0;assert(manual_params_valid(&five));
 assert(manual_gear_supported(&five,5)&&!manual_gear_supported(&five,6));
 ManualState absent={0},unchanged=absent;
 ManualOutput rejected=manual_step(&absent,&five,(ManualInput){6,0,0,.01});
 assert(!rejected.valid&&!memcmp(&absent,&unchanged,sizeof(absent)));
 absent=(ManualState){.mode=MANUAL_ENGAGED,.gear=6};
 assert(!manual_step(&absent,&five,(ManualInput){5,0,0,.01}).valid);
 ManualState s={.engine_omega=100,.input_omega=70,.output_omega=20};
 ManualOutput o=manual_step(&s,&p,(ManualInput){0,0,0,.01});assert(o.valid&&s.mode==MANUAL_NEUTRAL);near(s.input_omega,70);near(s.engine_omega,100);
 s=(ManualState){.engine_omega=70,.input_omega=70,.output_omega=20};
 o=manual_step(&s,&p,(ManualInput){1,1,0,.01});assert(o.valid&&s.mode==MANUAL_ENGAGED&&!o.grinding); /* clutchless rev match */
 o=manual_step(&s,&p,(ManualInput){2,1,0,.01});assert(o.left_gear&&s.mode==MANUAL_NEUTRAL&&s.gear==0);
 s=(ManualState){.engine_omega=1000,.input_omega=1000};
 for(int i=0;i<30;i++)o=manual_step(&s,&p,(ManualInput){1,1,0,.01});
 assert(s.mode==MANUAL_SYNCHRONIZING&&o.grinding); /* sustained mismatch/load */
 s=(ManualState){.engine_omega=1000,.input_omega=200,.output_omega=10};
 for(int i=0;i<1000&&s.mode!=MANUAL_ENGAGED;i++)o=manual_step(&s,&p,(ManualInput){1,0,0,.01});
 assert(s.mode==MANUAL_ENGAGED);near(s.engine_omega,1000);
 s=(ManualState){.engine_omega=100};o=manual_step(&s,&p,(ManualInput){-1,0,1,.01});assert(o.reverse_blocked&&s.mode==MANUAL_NEUTRAL);
 o=manual_step(&s,&p,(ManualInput){-1,0,0,.01});assert(s.mode==MANUAL_ENGAGED);
 o=manual_step(&s,&p,(ManualInput){-1,0,-3,.01});assert(s.mode==MANUAL_ENGAGED&&!o.reverse_blocked);
 s=(ManualState){.engine_omega=3};
 o=manual_step(&s,&p,(ManualInput){1,1,0,.01});
 assert(fabs(s.input_omega-p.ratios[2]*s.output_omega)<p.match_rad_s);
 assert(fabs(o.clutch_torque)>p.max_engagement_load&&s.mode==MANUAL_SYNCHRONIZING); /* matched but still loaded */
 ManualState before=s;o=manual_step(&s,&p,(ManualInput){1,NAN,0,.01});assert(!o.valid&&!memcmp(&s,&before,sizeof(s)));
 o=manual_step(&s,&p,(ManualInput){1,0,0,0});assert(!o.valid);
 o=manual_step(&s,&p,(ManualInput){7,0,0,.01});assert(!o.valid);
 p.input_inertia=0;o=manual_step(&s,&p,(ManualInput){1,0,0,.01});assert(!o.valid);
 printf("PASS %u energy/bounds/reaction steps plus state and fault cases\n",cases);
}
