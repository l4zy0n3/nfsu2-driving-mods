// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef MANUAL_CORE_H
#define MANUAL_CORE_H
#include <math.h>
#include <string.h>
/* Standalone SI model. Gear numbering: -1 reverse, 0 neutral, 1..6 forward. */
typedef enum { MANUAL_NEUTRAL, MANUAL_SYNCHRONIZING, MANUAL_ENGAGED } ManualMode;
typedef struct {
    double engine_inertia,input_inertia,output_inertia;
    double clutch_capacity,synch_capacity,match_rad_s,max_engagement_load;
    double grind_delay,reverse_max_mps;
    double ratios[8]; /* index gear+1 = native gear ID; zero means unavailable/neutral. */
} ManualParams;
typedef struct {
    double engine_omega,input_omega,output_omega,synch_time;
    ManualMode mode;
    int gear,target;
} ManualState;
typedef struct { int requested; double release,speed_mps,dt; } ManualInput;
typedef struct {
    int valid,grinding,reverse_blocked,left_gear;
    double clutch_torque,synch_torque,dog_torque;
    double clutch_work; /* Friction loss in model energy units; excludes dog projection. */
    double engine_reaction,input_reaction,output_reaction;
} ManualOutput;
static double manual_limit(double x,double bound)
{ return fmax(-bound,fmin(bound,x)); }
static int manual_params_valid(const ManualParams *p)
{
    if(!p)return 0;
    const double fields[]={p->engine_inertia,p->input_inertia,p->output_inertia,p->clutch_capacity,
        p->synch_capacity,p->match_rad_s,p->max_engagement_load,p->grind_delay,p->reverse_max_mps};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)
        if(!isfinite(fields[i])||fields[i]<=0||fields[i]>1e8)return 0;
    if(p->engine_inertia<1e-8||p->input_inertia<1e-8||p->output_inertia<1e-8)return 0;
    for(int i=0;i<8;i++)if(!isfinite(p->ratios[i])||fabs(p->ratios[i])>100)return 0;
    if(p->ratios[0]>0||p->ratios[1]!=0||p->ratios[2]<=0)return 0;
    for(int i=2;i<8;i++)if(p->ratios[i]<0)return 0;
    return 1;
}
static int manual_gear_supported(const ManualParams *p,int gear)
{ return p&&gear>= -1&&gear<=6&&(gear==0||p->ratios[gear+1]!=0); }
/* Caller applies external engine/road torques separately before this step. This
   step changes all three angular velocities and returns the matching reactions.
   Do not apply those reactions again to these already-advanced velocities. */
static ManualOutput manual_step(ManualState *state,const ManualParams *p,ManualInput in)
{
    ManualOutput out={0};
    if(!state||!manual_params_valid(p)||!manual_gear_supported(p,in.requested)||
       !isfinite(in.release)||in.release<0||in.release>1||!isfinite(in.speed_mps)||
       !isfinite(in.dt)||in.dt<1e-6||in.dt>0.1||
       !isfinite(state->engine_omega)||fabs(state->engine_omega)>1e8||
       !isfinite(state->input_omega)||fabs(state->input_omega)>1e8||
       !isfinite(state->output_omega)||fabs(state->output_omega)>1e8||
       !isfinite(state->synch_time)||state->synch_time<0||
       state->mode<MANUAL_NEUTRAL||state->mode>MANUAL_ENGAGED||
       state->gear< -1||state->gear>6||state->target< -1||state->target>6||
       (state->mode==MANUAL_ENGAGED&&(state->gear==0||!manual_gear_supported(p,state->gear))))return out;
    ManualState s=*state;
    int request=in.requested;
    if(request== -1&&!(s.mode==MANUAL_ENGAGED&&s.gear== -1)&&fabs(in.speed_mps)>p->reverse_max_mps){request=0;out.reverse_blocked=1;}
    if(s.mode==MANUAL_ENGAGED&&request!=s.gear){
        s.mode=MANUAL_NEUTRAL;s.gear=0;s.target=0;s.synch_time=0;out.left_gear=1;
    }
    if(s.mode!=MANUAL_ENGAGED) {
        if(request!=s.target){s.synch_time=0;s.target=request;}
        s.mode=request&&!out.left_gear?MANUAL_SYNCHRONIZING:MANUAL_NEUTRAL;
        s.gear=0;
    }
    double cap=p->clutch_capacity*in.release*in.release;
    if(s.mode==MANUAL_ENGAGED) {
        double ratio=p->ratios[s.gear+1];
        /* First project externally advanced shafts onto the rigid dog constraint. */
        double dog=-(s.input_omega-ratio*s.output_omega)/
            (1/p->input_inertia+ratio*ratio/p->output_inertia);
        s.input_omega+=dog/p->input_inertia;s.output_omega-=ratio*dog/p->output_inertia;
        /* Clutch acts on the entire connected load, not the tiny free input shaft. */
        double reflected=p->output_inertia/(ratio*ratio);
        double connected=p->input_inertia+reflected;
        double slip_before=s.engine_omega-s.input_omega;
        double jc=manual_limit((s.engine_omega-s.input_omega)/
            (1/p->engine_inertia+1/connected),cap*in.dt);
        out.clutch_work=fmax(0,jc*slip_before-.5*jc*jc*(1/p->engine_inertia+1/connected));
        s.engine_omega-=jc/p->engine_inertia;
        s.input_omega+=jc/connected;s.output_omega+=jc/(ratio*connected);
        dog-=jc*reflected/connected;
        out.clutch_torque=jc/in.dt;out.dog_torque=dog/in.dt;
        out.engine_reaction=-out.clutch_torque;
        out.input_reaction=out.clutch_torque+out.dog_torque;
        out.output_reaction=-ratio*out.dog_torque;
    } else {
        /* A Coulomb friction impulse cannot cross the two-shaft equal-speed point. */
        double slip_before=s.engine_omega-s.input_omega;
        double jc=manual_limit((s.engine_omega-s.input_omega)/
            (1/p->engine_inertia+1/p->input_inertia),cap*in.dt);
        out.clutch_work=fmax(0,jc*slip_before-.5*jc*jc*(1/p->engine_inertia+1/p->input_inertia));
        s.engine_omega-=jc/p->engine_inertia;s.input_omega+=jc/p->input_inertia;
        out.clutch_torque=jc/in.dt;out.engine_reaction=-out.clutch_torque;
        out.input_reaction=out.clutch_torque;
        if(s.mode==MANUAL_SYNCHRONIZING) {
            double ratio=p->ratios[request+1];
            double mismatch=s.input_omega-ratio*s.output_omega;
            double impulse=manual_limit(-mismatch/(1/p->input_inertia+ratio*ratio/p->output_inertia),p->synch_capacity*in.dt);
            s.input_omega+=impulse/p->input_inertia;
            s.output_omega-=ratio*impulse/p->output_inertia;
            out.synch_torque=impulse/in.dt;
            out.input_reaction+=out.synch_torque;out.output_reaction=-ratio*out.synch_torque;
            s.synch_time=fmin(s.synch_time+in.dt,1e6);
            double remaining=fabs(s.input_omega-ratio*s.output_omega);
            /* Applied clutch load, not pedal depth, determines whether the dogs unload. */
            if(remaining<=p->match_rad_s&&fabs(out.clutch_torque)<=p->max_engagement_load){
                s.mode=MANUAL_ENGAGED;s.gear=request;s.synch_time=0;
            } else out.grinding=s.synch_time>=p->grind_delay;
        }
    }
    if(!isfinite(s.engine_omega)||!isfinite(s.input_omega)||!isfinite(s.output_omega)||
       !isfinite(out.clutch_work)||!isfinite(out.engine_reaction)||!isfinite(out.input_reaction)||!isfinite(out.output_reaction))
        return (ManualOutput){0};
    *state=s;out.valid=1;return out;
}
#endif
