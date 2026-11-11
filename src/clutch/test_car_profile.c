// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include "car-profile.h"
#include "manual_core.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
 CarClutchProfile d=car_clutch_defaults(),soft={12,90,125},sport={22,80,135},bad={-1,101,0};
 assert(car_clutch_equal(car_clutch_resolve(bad,soft),soft));
 assert(car_clutch_equal(car_clutch_resolve((CarClutchProfile){90,20,301},sport),sport));
 assert(car_clutch_equal(car_clutch_resolve(bad,bad),d));
 assert(car_clutch_resolve((CarClutchProfile){15,80,49},soft).capacity_percent==125);
 CarClutchProfile profiles[]={d,soft,{18,82,125},{15,85,130},sport,{25,78,145},{18,90,140},{10,94,145},{0,100,300},{99,100,50}};
 for(unsigned i=0;i<sizeof(profiles)/sizeof(*profiles);i++){
  CarClutchProfile profile=profiles[i];assert(car_clutch_profile_valid(profile));
  assert(car_clutch_release(profile,0)==0&&car_clutch_release(profile,1)==1);
  assert(car_clutch_release(profile,NAN)==0&&car_clutch_release(profile,-INFINITY)==0);
  double previous=0;
  for(int tick=0;tick<=1000;tick++){
   double release=car_clutch_release(profile,tick/1000.0);assert(release>=previous&&release>=0&&release<=1);previous=release;
  }
  /* A fully pressed pedal never loads the engine, including during a shift.
     Dog/synchronizer reactions on the gearbox side may still be nonzero. */
  ManualParams p={.engine_inertia=.00026,.input_inertia=.000026,.output_inertia=.001,
   .clutch_capacity=.243*profile.capacity_percent/100,.synch_capacity=.243*.15,
   .match_rad_s=50/9.549304,.max_engagement_load=.243*.05,.grind_delay=.15,.reverse_max_mps=.1,
   .ratios={-3,0,3,2,1,.8,.7,.6}};
  for(int gear=-1;gear<=6;gear++)for(int mode=0;mode<=2;mode++){
   ManualState state={.engine_omega=600,.input_omega=20,.output_omega=7,.mode=mode,.gear=mode==MANUAL_ENGAGED?1:0};
   ManualOutput out=manual_step(&state,&p,(ManualInput){gear,car_clutch_release(profile,0),0,.01});
   assert(out.valid&&out.clutch_torque==0&&out.engine_reaction==0&&state.engine_omega==600);
  }
 }
 const double weights[4][9]={{0,560,360,175,160,140,140,0,0},{0,600,400,230,220,160,160,0,0},
  {0,850,550,300,250,200,180,0,0},{0,1100,750,400,325,290,230,0,0}};
 for(int cls=0;cls<4;cls++)for(int gear=2;gear<=7;gear++){
  double expected=8000-(double).95f*(weights[cls][gear]*1.2);
  assert(car_native_shift_light_rpm(gear,cls,8000,weights[cls][gear],1.2,.95f,8500)==expected);
 }
 assert(car_native_shift_light_rpm(7,0,9000,0,1,.95f,8500)==9000);
 assert(car_native_shift_light_rpm(1,0,8000,400,1,.95f,8500)==8500);
 assert(car_native_shift_light_rpm(2,-1,8000,400,1,.95f,8500)==8500);
 assert(car_native_shift_light_rpm(2,4,8000,400,1,.95f,8500)==8500);
 assert(car_native_shift_light_rpm(2,0,NAN,400,1,.95f,8500)==8500);
 assert(car_native_shift_light_rpm(2,0,8000,400,-1,.95f,8500)==8500);
 assert(car_native_shift_light_rpm(2,0,8000,400,1,.95f,NAN)==0);
 puts("PASS authored profile fallback/limits/monotonic travel/full-disengagement reactions and native shift target bounds");
}
