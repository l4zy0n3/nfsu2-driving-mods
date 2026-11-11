// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include "native_coeff.h"
#include "manual_core.h"
#include <assert.h>
#include <stdio.h>
static void close_to(double a,double b){assert(fabs(a-b)<1e-9*fmax(1,fmax(fabs(a),fabs(b))));}
int main(void)
{
 NativeWheels w={.omega={18,20,17,19},.inertia={.01,.01,.01,.01},.front_final=3.6365,.rear_final=3.6365,.rear_share=.50000006,.threshold=.3};
 double c[4],omega,j,accel;
 for(int mode=0;mode<3;mode++)for(int mask=0;mask<16;mask++)for(int contacts=0;contacts<16;contacts++){
  w.mode=mode;for(int i=0;i<4;i++){w.disabled[i]=(mask>>i)&1;w.omega[i]=w.disabled[i]?0:18+i;w.contact[i]=(contacts>>i)&1;w.torque[i]=i*.01;}
  assert(native_coefficients(w,c,&omega,&j,&accel));
  double power=0,response=0,predict=0;
  for(int i=0;i<4;i++){power+=c[i]*.3*w.omega[i];if(!w.disabled[i]){response+=c[i]*(c[i]*.3/w.inertia[i]);predict+=c[i]*w.torque[i]/w.inertia[i];}}
  close_to(power,.3*omega);if(mask<15)close_to(response,.3/j);else{close_to(response,0);assert(j==1e8);}close_to(predict,accel);
 }
 close_to(native_pair_weight(1,4,1,0,.3),.85);close_to(native_pair_weight(3,4,1,0,.3),.15);
 close_to(native_pair_weight(2,4,0,0,.3),.5);close_to(native_pair_weight(0,0,1,1,.3),.7);
 w.disabled[0]=w.disabled[1]=w.disabled[2]=w.disabled[3]=1;assert(native_coefficients(w,c,&omega,&j,&accel)&&j==1e8&&omega==0);
 w.disabled[0]=0;w.inertia[0]=NAN;assert(!native_coefficients(w,c,&omega,&j,&accel));
 /* Actual snapshot units; never substitute SI fixture values into the adapter. */
 ManualParams p={.engine_inertia=.0002435,.input_inertia=.00002435,.output_inertia=.01/(3.6365*3.6365*.25),
 .clutch_capacity=.338972*1.25,.synch_capacity=.338972*.15,.match_rad_s=50/9.549304,.max_engagement_load=.338972*.05,
 .grind_delay=.15,.reverse_max_mps=.1,.ratios={-3.8,0,3.827,2.36,1.69,1.31,1.05,.9098}};
 for(int hz=120;hz<=960;hz*=2){
  ManualState s={.engine_omega=300,.input_omega=300,.output_omega=300/3.827,.mode=MANUAL_ENGAGED,.gear=1,.target=1};
  for(int i=0;i<hz;i++){
   s.engine_omega+=.1/p.engine_inertia/hz;s.output_omega-=3.827*.1/p.output_inertia/hz;
   ManualOutput o=manual_step(&s,&p,(ManualInput){1,1,0,1.0/hz});assert(o.valid);
   close_to(o.clutch_torque,.1);close_to(o.output_reaction,3.827*.1);close_to(s.engine_omega,300);
  }
 }
 /* Holding the brake in first gear must retain finite engine clutch load. */
 p.output_inertia=1e8;
 ManualState locked={.engine_omega=100,.input_omega=0,.output_omega=0,.mode=MANUAL_ENGAGED,.gear=1,.target=1};
 ManualOutput held=manual_step(&locked,&p,(ManualInput){1,1,0,.004});
 assert(held.valid&&held.engine_reaction<0&&isfinite(held.output_reaction));
 assert(fabs(held.engine_reaction)<=p.clutch_capacity&&fabs(locked.output_omega)<1e-8);
 puts("PASS native coefficient power/inertia/disabled-wheel checks and actual-unit loaded fixture");
}
