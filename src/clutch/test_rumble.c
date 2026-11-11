// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include "rumble-core.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
 RumbleMotion motion={0};RumbleSchedule schedule={0};
 RumbleSample sample=rumble_motion(&motion,3,10,1,0,.01,1000);
 assert(sample.valid&&fabs(sample.slip-.3)<1e-12&&sample.kick==1);
 RumbleLevels strong=rumble_levels(sample,1,50,1,1,1000);
 assert(strong.left>0&&strong.right>0&&strong.left<=32767&&strong.right<=32767);
 assert(rumble_due(&schedule,strong,1000));
 RumbleLevels changed={1,1};assert(!rumble_due(&schedule,changed,1039));assert(rumble_due(&schedule,changed,1040));
 assert(rumble_due(&schedule,(RumbleLevels){0},1041));assert(!rumble_due(&schedule,(RumbleLevels){0},1042));
 for(int i=0;i<100;i++)sample=rumble_motion(&motion,0,10,1,0,.01,1001+i);
 assert(sample.kick<.00001);assert(rumble_levels(sample,1,35,1,1,sample.tick).left==0);
 sample=rumble_motion(&motion,0,0,0,1,.01,1101);
 RumbleLevels grind=rumble_levels(sample,1,35,1,1,1101);assert(grind.right>0);
 assert(!rumble_levels(sample,0,35,1,1,1101).right);
 assert(!rumble_levels(sample,1,35,0,1,1101).right);
 assert(!rumble_levels(sample,1,35,1,0,1101).right);
 assert(!rumble_levels(sample,1,51,1,1,1101).right);
 assert(!rumble_levels(sample,1,-1,1,1,1101).right);
 assert(!rumble_levels(sample,1,35,1,1,1202).right);
 sample.tick=UINT32_MAX-20;assert(rumble_levels(sample,1,35,1,1,20).right>0);
 assert(!rumble_levels(sample,1,35,1,1,100).right);
 sample=rumble_motion(&motion,NAN,1,1,0,.01,1);assert(!sample.valid&&motion.kick==0);
 assert(!rumble_motion(&motion,1,INFINITY,1,0,.01,1).valid);
 assert(!rumble_motion(&motion,1,1,-1,0,.01,1).valid);
 assert(!rumble_motion(&motion,1,1,1,0,0,1).valid);
 for(int i=0;i<10000;i++){
  sample=rumble_motion(&motion,i*.1,-i*.01,i*.1,i%2,.004,(uint32_t)i);
  RumbleLevels levels=rumble_levels(sample,1,50,1,1,(uint32_t)i);
  assert(levels.left<=32767&&levels.right<=32767&&sample.slip>=0&&sample.slip<=1&&sample.kick>=0&&sample.kick<=1);
 }
 puts("PASS Xbox cue bounds, pulse decay, rate limit, immediate stop, stale/focus/disconnect/disable, wrap and invalid samples");
}
