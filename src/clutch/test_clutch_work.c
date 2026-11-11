// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include <assert.h>
#include <stdio.h>
#include "manual_core.h"
static double energy(ManualState s,ManualParams p){return .5*(p.engine_inertia*s.engine_omega*s.engine_omega+p.input_inertia*s.input_omega*s.input_omega+p.output_inertia*s.output_omega*s.output_omega);}
int main(void){
 ManualParams p={.engine_inertia=1,.input_inertia=.1,.output_inertia=5,.clutch_capacity=100,.synch_capacity=1,.match_rad_s=1,.max_engagement_load=1,.grind_delay=1,.reverse_max_mps=1,.ratios={-3,0,3,2,1,1,1,1}};
 for(int mode=0;mode<2;mode++)for(int sign=-1;sign<=1;sign+=2){
  ManualState s={.engine_omega=sign*100,.input_omega=30,.output_omega=10,.mode=mode?MANUAL_ENGAGED:MANUAL_NEUTRAL,.gear=mode?1:0};
  double before=energy(s,p);ManualOutput o=manual_step(&s,&p,(ManualInput){mode?1:0,1,0,.01});
  assert(o.valid&&o.clutch_work>=0&&fabs(before-energy(s,p)-o.clutch_work)<1e-8);
 }
 ManualState s={.engine_omega=100,.input_omega=60,.output_omega=10,.mode=MANUAL_ENGAGED,.gear=1};
 double before=energy(s,p);ManualOutput o=manual_step(&s,&p,(ManualInput){1,0,0,.01});
 assert(o.valid&&o.clutch_work==0&&before>energy(s,p)); /* Dog projection loss excluded. */
 puts("Clutch work matches pure friction energy decrease; dog projection excluded.");return 0;
}
