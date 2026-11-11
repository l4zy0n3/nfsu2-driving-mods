// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_NATIVE_COEFF_H
#define U2_NATIVE_COEFF_H
#include <math.h>
typedef struct {double omega[4],inertia[4],torque[4],front_final,rear_final,rear_share,threshold;int disabled[4],contact[4],mode;} NativeWheels;
/* Exact distribution policy from installed 0x594610; no gear ratio included. */
static double native_pair_weight(double numerator,double sum,int both,int mode,double threshold)
{
 double t=both?threshold:.5;
 if(mode==1)return 1-t;
 if(mode==2)return t;
 if(fabs(sum)<=1e-5)return .5;
 double q=numerator/sum;
 if(q<t)return 1-threshold*.5;
 if(q>1-t)return threshold*.5;
 return q;
}
static int native_coefficients(NativeWheels w,double c[4],double *omega,double *inertia,double *accel)
{
 if(!isfinite(w.front_final)||!isfinite(w.rear_final)||w.front_final<0||w.rear_final<0||
 !isfinite(w.rear_share)||w.rear_share<0||w.rear_share>1||!isfinite(w.threshold)||w.threshold<0||w.threshold>.5)return 0;
 for(int i=0;i<4;i++)if(!isfinite(w.omega[i])||!isfinite(w.inertia[i])||w.inertia[i]<=0||!isfinite(w.torque[i]))return 0;
 double f=native_pair_weight(w.omega[1],w.omega[0]+w.omega[1],w.contact[0]&&w.contact[1],w.mode,w.threshold);
 double r=native_pair_weight(w.omega[2],w.omega[2]+w.omega[3],w.contact[2]&&w.contact[3],w.mode,w.threshold);
 double front=(1-w.rear_share)*w.front_final,rear=w.rear_share*w.rear_final;
 c[0]=front*(1-f);c[1]=front*f;c[2]=rear*r;c[3]=rear*(1-r);
 double inverse=0;*omega=0;*accel=0;
 for(int i=0;i<4;i++){
  *omega+=c[i]*w.omega[i];
  if(!w.disabled[i]){inverse+=c[i]*c[i]/w.inertia[i];*accel+=c[i]*w.torque[i]/w.inertia[i];}
 }
 if(!isfinite(inverse)||inverse<0||!isfinite(*omega)||!isfinite(*accel))return 0;
 /* Native flag10c locks omega to zero. A bounded fixed-load approximation
    preserves clutch reaction while native braking absorbs the wheel reaction. */
 *inertia=inverse>0?1/inverse:1e8;return isfinite(*inertia);
}
#endif
