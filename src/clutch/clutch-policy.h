// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_CLUTCH_POLICY_H
#define U2_CLUTCH_POLICY_H
#include <math.h>
/* The game's pre-race hold forces neutral; it must also own the drivetrain. */
static inline int clutch_physics_eligible(int realism,int transmission,int pre_race_hold)
{return realism&&transmission==1&&!pre_race_hold;}
static inline int clutch_neutral_coast(int native_rest,int eligible,int committed,int requested,float brake,float handbrake)
{
 /* A released controller can report one 8-bit count; retain its actual brake force. */
 const float noise=1.0f/255.0f;
 return (native_rest==1||native_rest==2)&&eligible&&committed==1&&requested==1&&
     isfinite(brake)&&brake>=0&&brake<=noise&&
     isfinite(handbrake)&&handbrake>=0&&handbrake<=noise;
}
typedef struct {int target,reason;} ClutchTarget; /* native IDs: R0,N1,first2..sixth7 */
static inline unsigned clutch_gear_mask(const double ratios[8])
{
 unsigned mask=2;
 for(int i=0;i<8;i++)if(i!=1&&isfinite(ratios[i])&&(i==0?ratios[i]<0:ratios[i]>0))mask|=1u<<i;
 return mask;
}
static inline ClutchTarget clutch_choose_target(int request,int committed,unsigned mask)
{
 int reason=request<0||request>7?1:!(mask&(1u<<request))?2:0;
 if(!reason)return (ClutchTarget){request,0};
 int fallback=committed>=0&&committed<=7&&(mask&(1u<<committed))?committed:1;
 return (ClutchTarget){fallback,reason};
}
/* Generic estimates, not factory measurements. Bounds are configuration limits. */
typedef struct {int input_percent,capacity_percent,match_rpm,load_percent,grind_ms,reverse_cms;} ClutchEstimates;
static inline ClutchEstimates clutch_estimate_defaults(void)
{return (ClutchEstimates){10,15,50,5,150,10};}
static inline int clutch_bounded(int n,int lo,int hi,int fallback)
{return n>=lo&&n<=hi?n:fallback;}
static inline ClutchEstimates clutch_estimates_validate(ClutchEstimates p)
{
 p.input_percent=clutch_bounded(p.input_percent,1,100,10);
 p.capacity_percent=clutch_bounded(p.capacity_percent,1,100,15);
 p.match_rpm=clutch_bounded(p.match_rpm,1,500,50);
 p.load_percent=clutch_bounded(p.load_percent,1,100,5);
 p.grind_ms=clutch_bounded(p.grind_ms,20,2000,150);
 p.reverse_cms=clutch_bounded(p.reverse_cms,1,100,10);
 return p;
}
static inline int clutch_estimates_equal(ClutchEstimates a,ClutchEstimates b)
{return a.input_percent==b.input_percent&&a.capacity_percent==b.capacity_percent&&a.match_rpm==b.match_rpm&&a.load_percent==b.load_percent&&a.grind_ms==b.grind_ms&&a.reverse_cms==b.reverse_cms;}
/* One proxy writer at a time. Rearm only on the owned -> native edge. */
enum {CLUTCH_PROXY_NATIVE,CLUTCH_PROXY_SKIP,CLUTCH_PROXY_REARM};
static inline int clutch_proxy_action(int *owned,int eligible)
{
 if(eligible){*owned=1;return CLUTCH_PROXY_SKIP;}
 if(*owned){*owned=0;return CLUTCH_PROXY_REARM;}
 return CLUTCH_PROXY_NATIVE;
}
#endif
