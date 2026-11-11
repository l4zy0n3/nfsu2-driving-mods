// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_CLUTCH_CORE_H
#define U2_CLUTCH_CORE_H
#include <math.h>

typedef struct { float twist,torque; int sliding; } ClutchState;
static float clutch_clamp(float x,float lo,float hi)
{ return fminf(hi,fmaxf(lo,x)); }

/* Independent elastic/friction model; all torques and inertias use native U2 units. */
static float clutch_step(ClutchState *s,float slip,float release,float peak,
                        float inertia,float dt,float bite_start,float bite_end,
                        float strength,float frequency,float damping)
{
    if(!isfinite(slip)||!isfinite(release)||!isfinite(peak)||peak<=0||
       !isfinite(inertia)||inertia<=0||!isfinite(dt)||dt<=0||dt>0.1f||
       !isfinite(bite_start)||!isfinite(bite_end)||bite_start<0||bite_end>1||bite_start>=bite_end||
       !isfinite(strength)||strength<=0||!isfinite(frequency)||frequency<=0||
       !isfinite(damping)||damping<0) { *s=(ClutchState){0};return 0; }
    float pressure=clutch_clamp((release-bite_start)/(bite_end-bite_start),0,1);
    float capacity=peak*strength*pressure*pressure;
    if(capacity<=0) { *s=(ClutchState){0};return 0; }
    float omega=6.28318530718f*frequency;
    float k=inertia*omega*omega,c=2*damping*inertia*omega;
    float denominator=1+dt*(c+dt*k)/inertia;
    if(!isfinite(capacity)||!isfinite(k)||k<=0||!isfinite(c)||!isfinite(denominator)||denominator<=0) {
        *s=(ClutchState){0};return 0;
    }
    if(!isfinite(s->twist))s->twist=0;
    s->twist=clutch_clamp(s->twist,-capacity/k,capacity/k);
    /* Backward Euler includes the reaction on both shafts, avoiding an explicit stiff spring. */
    float demand=(k*(s->twist+dt*slip)+c*slip)/denominator;
    if(!isfinite(demand)) { *s=(ClutchState){0};return 0; }
    float torque=clutch_clamp(demand,-capacity,capacity);
    float next_slip=slip-dt*torque/inertia;
    s->twist=clutch_clamp(s->twist+dt*next_slip,-capacity/k,capacity/k);
    s->torque=torque;s->sliding=fabsf(demand)>capacity;
    return torque;
}
#endif
