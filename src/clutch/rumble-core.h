// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_RUMBLE_CORE_H
#define U2_RUMBLE_CORE_H
#include <math.h>
#include <stdint.h>
typedef struct {double previous_clutch,kick;} RumbleMotion;
typedef struct {double slip,kick;int grinding,valid;uint32_t tick;} RumbleSample;
typedef struct {uint16_t left,right;} RumbleLevels;
typedef struct {RumbleLevels last;uint32_t tick;int sent;} RumbleSchedule;
static double rumble_clamp(double value){return fmax(0,fmin(1,value));}
/* Generic normalized cues; neither signal is calibrated motor torque. */
static RumbleSample rumble_motion(RumbleMotion *state,double slip_speed,double road_speed,
 double clutch_load,int grinding,double dt,uint32_t tick)
{
 if(!state||!isfinite(slip_speed)||slip_speed<0||!isfinite(road_speed)||
    !isfinite(clutch_load)||clutch_load<0||!isfinite(dt)||dt<=0||dt>.1){
  if(state)*state=(RumbleMotion){0};
  return (RumbleSample){0};
 }
 double load=rumble_clamp(clutch_load);
 state->kick=fmax(state->kick*exp(-dt/.08),fmax(0,load-state->previous_clutch));
 state->previous_clutch=load;
 return (RumbleSample){rumble_clamp(slip_speed/fmax(3,fabs(road_speed))),state->kick,
   grinding!=0,1,tick};
}
static RumbleLevels rumble_levels(RumbleSample sample,int enabled,int gain,int connected,int focused,uint32_t now)
{
 if(!enabled||gain<0||gain>50||!connected||!focused||!sample.valid||now-sample.tick>100||
    !isfinite(sample.slip)||!isfinite(sample.kick))return (RumbleLevels){0};
 double scale=gain*.01*65535;
 return (RumbleLevels){(uint16_t)(scale*rumble_clamp(.70*sample.slip+sample.kick)),
   (uint16_t)(scale*rumble_clamp(.25*sample.slip+.45*sample.kick+.70*sample.grinding))};
}
/* Nonzero commands are limited to 25 Hz. A stop bypasses the rate limit. */
static int rumble_due(RumbleSchedule *state,RumbleLevels next,uint32_t now)
{
 int zero=!next.left&&!next.right;
 if(zero){if(!state->last.left&&!state->last.right)return 0;}
 else if(state->sent&&now-state->tick<40)return 0;
 if(state->sent&&next.left==state->last.left&&next.right==state->last.right)return 0;
 state->last=next;state->tick=now;state->sent=1;return 1;
}
#endif
