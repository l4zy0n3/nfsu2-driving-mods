// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_CAR_PROFILE_H
#define U2_CAR_PROFILE_H
#include <math.h>
/* Authored pedal/capacity estimates. Torque and inertia stay in native units. */
typedef struct {int bite_start_percent,bite_end_percent,capacity_percent;} CarClutchProfile;
static inline CarClutchProfile car_clutch_defaults(void)
{return (CarClutchProfile){5,95,125};}
static inline int car_clutch_profile_valid(CarClutchProfile p)
{return p.bite_start_percent>=0&&p.bite_end_percent<=100&&p.bite_start_percent<p.bite_end_percent&&p.capacity_percent>=50&&p.capacity_percent<=300;}
static inline CarClutchProfile car_clutch_resolve(CarClutchProfile p,CarClutchProfile fallback)
{
 if(!car_clutch_profile_valid(fallback))fallback=car_clutch_defaults();
 if(p.bite_start_percent<0||p.bite_start_percent>99)p.bite_start_percent=fallback.bite_start_percent;
 if(p.bite_end_percent<1||p.bite_end_percent>100)p.bite_end_percent=fallback.bite_end_percent;
 if(p.bite_start_percent>=p.bite_end_percent){p.bite_start_percent=fallback.bite_start_percent;p.bite_end_percent=fallback.bite_end_percent;}
 if(p.capacity_percent<50||p.capacity_percent>300)p.capacity_percent=fallback.capacity_percent;
 return p;
}
static inline int car_clutch_equal(CarClutchProfile a,CarClutchProfile b)
{return a.bite_start_percent==b.bite_start_percent&&a.bite_end_percent==b.bite_end_percent&&a.capacity_percent==b.capacity_percent;}
static inline double car_clutch_release(CarClutchProfile p,double released)
{
 if(!car_clutch_profile_valid(p)||!isfinite(released))return 0;
 double value=(released*100-p.bite_start_percent)/(p.bite_end_percent-p.bite_start_percent);
 return fmax(0,fmin(1,value));
}
/* Exact-build HUD0x4c67ef..0x4c6865: strict RPM > this threshold.
   A threshold above limiter is meaningful (e.g. no final-gear shift cue).
   Unknown/malformed metadata uses the validated limiter. */
static inline double car_native_shift_light_rpm(int gear,int performance_class,double upshift,double window_weight,double window_scale,double fraction,double limit)
{
 if(!isfinite(limit)||limit<=0||limit>100000)return 0;
 if(gear<2||gear>7||performance_class<0||performance_class>3||!isfinite(upshift)||
  !isfinite(window_weight)||window_weight<0||!isfinite(window_scale)||window_scale<0||
  !isfinite(fraction)||fraction<0||fraction>1)return limit;
 double target=upshift-fraction*(window_weight*window_scale);
 return isfinite(target)&&target>0&&target<=100000?target:limit;
}
#endif
