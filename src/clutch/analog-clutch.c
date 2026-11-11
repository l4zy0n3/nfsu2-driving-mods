// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include <math.h>
#include <string.h>
#include "wheel-buttons.h"
#include "clutch-core.h"
#include "clutch-policy.h"
#include "sync-step.h"
#include "car-profile.h"
#ifndef SELF_TEST
#include "handbrake-core.h"
#include "manual_core.h"
#include "native_coeff.h"
#endif

static const char *starter_message(int active,int stalled,int starter,int clutch_down)
{
    if(!active)return NULL;
    if(!stalled)return "REALISTIC | ENGINE RUNNING";
    if(!clutch_down)return starter?"STARTER BLOCKED | CLUTCH FULL DOWN":
        "ENGINE STALLED | CLUTCH FULL DOWN + HOLD IGNITION";
    return starter?"STARTING | KEEP CLUTCH DOWN + HOLD IGNITION":
        "ENGINE STALLED | HOLD IGNITION TO START";
}

static int starter_interlock(int requested,float foot)
{return requested&&isfinite(foot)&&foot>=0&&foot<=.05f;}
static float starter_torque(float drag,float peak,int cranking)
{
    if(!cranking||!isfinite(drag)||!isfinite(peak)||peak<=0)return drag;
    /* Native closed-throttle drag can cancel the starter at zero RPM. */
    return fmaxf(drag+peak*.35f,peak*.05f);
}

static float engagement(long raw,long released,long pressed)
{
    if (released==pressed) return 1.0f;
    double x=1.0-((double)raw-released)/((double)pressed-released);
    return x<0?0:x>1?1:(float)x;
}
typedef struct {int stalled;float low_time,ready_time;} EngineState;
static void engine_step(EngineState *s,float rpm,float idle,float dt,int starter,int isolated)
{
    if(!isfinite(rpm)||!isfinite(idle)||idle<=0||!isfinite(dt)||dt<=0||dt>0.1f)return;
    if(s->ready_time<1.0f)s->ready_time+=dt;
    if(s->stalled) {
        if((starter&&isolated&&rpm>=idle*0.75f)||(!isolated&&rpm>=idle*0.90f)) {
            s->stalled=0;s->low_time=0;
        }
    } else {
        if(s->ready_time<1.0f)return;
        s->low_time=rpm<idle*0.45f?s->low_time+dt:0;
        if(s->low_time>=0.15f){s->stalled=1;s->low_time=0;}
    }
}
static float idle_assist(float native,float rpm,float idle,float peak)
{
    if(rpm>=idle||idle<=0||peak<=0)return native;
    float error=(idle-rpm)/(idle*0.35f);
    if(error>1)error=1;
    float assist=0.12f*peak*error;
    return native>assist?native:assist;
}
static float pedal(float value)
{return !isfinite(value)||value<0?0:value>1?1:value;}
static float passive_torque(float torque,float shaft_speed,int stalled)
{
    if(!stalled)return torque;
    if(!isfinite(torque)||!isfinite(shaft_speed)||fabsf(shaft_speed)<0.001f)return 0;
    return torque*shaft_speed>0?0:torque;
}
static int manual_gear(int requested,int actual,float engaged,float speed)
{
    if(requested<0||requested>8||!isfinite(engaged)||!isfinite(speed))return actual;
    if(requested==actual||requested==1)return requested;
    if(engaged>0.05f)return actual;
    if((requested==0||actual==0)&&fabsf(speed)>0.1f)return actual;
    return requested;
}
static int controller_request(int requested,int actual,int pending,int maximum,float foot,float speed,int synchronizing)
{
    if(pending<0||pending>maximum)pending=actual;
    if(requested<0||requested>maximum||requested==actual)return pending;
    /* The synchronizer admits a lever target; its load/match checks engage the dogs. */
    if(synchronizing&&requested>1&&actual!=0&&isfinite(foot)&&isfinite(speed))return requested;
    return manual_gear(requested,actual,foot,speed);
}
static int controller_boundary(int direction,int requested,int pending,int actual,float foot,float speed)
{
    int target=direction<0&&requested==1&&pending==1?0:
               direction>0&&requested==0&&pending==0?1:-1;
    return target>=0&&manual_gear(target,actual,foot,speed)==target?target:-1;
}
static int controller_event_target(int direction,int before,int after,int pending,int actual,float foot,float speed)
{
    int boundary=controller_boundary(direction,before,pending,actual,foot,speed);
    if(boundary>=0)return boundary;
    if(after==before)return pending;
    return after==actual?actual:controller_request(after,actual,pending,7,foot,speed,1);
}
static int controller_routed_request(int before,int after)
{return before>=0&&before<=7?before:after;}
static int controller_native_steering(int gamepad,int verified,int captured)
{return gamepad&&verified&&captured;}
/* Feedback for the existing clutch interlock; no synchronizer or damage simulation. */
static int grinding_shift(int enabled,int paddles,int h,int actual,int admitted,int maximum,float foot,float rpm)
{
    return enabled&&!paddles&&h>=0&&h<=maximum&&h!=1&&h!=actual&&admitted!=h&&
        isfinite(foot)&&foot>.05f&&isfinite(rpm)&&rpm>200;
}
typedef struct {int last_h,paddles,selected,up_was,down_was;float auto_time;} ShiftState;
static int shift_request(ShiftState *s,int h,int up,int down,int actual,int maximum,
                         float foot,float speed,float dt,float *effective)
{
    if(s->last_h<0){s->last_h=h;s->selected=actual;}
    if(h!=s->last_h){s->last_h=h;s->paddles=0;s->auto_time=0;}
    int edge=(up&&!s->up_was&&!down)?1:(down&&!s->down_was&&!up)?-1:0;
    s->up_was=up;s->down_was=down;
    if(edge&&actual>=0&&actual<=maximum&&maximum>=2) {
        int previous=s->paddles?s->selected:actual;
        int target=previous+edge;
        if(target<0)target=0;
        if(target>maximum)target=maximum;
        if(target!=previous&&manual_gear(target,actual,0,speed)==target){s->selected=target;s->paddles=1;s->auto_time=0.16f;}
    }
    *effective=foot;
    if(!s->paddles)return manual_gear(h,actual,foot,speed);
    if(s->auto_time>0) {
        float age=0.16f-s->auto_time;
        *effective=fminf(foot,age<0.04f?0:pedal((age-0.04f)/0.12f));
        if(isfinite(dt)&&dt>0&&dt<=0.1f)s->auto_time=fmaxf(0,s->auto_time-dt);
    }
    int admitted=manual_gear(s->selected,actual,0,speed);
    if(admitted!=s->selected){s->selected=actual;s->auto_time=0;}
    return admitted;
}
static int synchronizer_request(ShiftState *s,int h,int request,int actual,float *effective)
{
    if(!s->paddles)return h;
    if(request!=actual){s->auto_time=.16f;*effective=0;}
    return request;
}
static void cancel_rejected_shift(ShiftState *s,int fallback)
{s->selected=fallback;s->auto_time=0;}
#ifdef SELF_TEST
#include <assert.h>
#include "manual_core.h"
int main(void)
{
    SyncStepClock clock={0};
    sync_step_capture(&clock,.004);sync_step_capture(&clock,0);
    assert(sync_step_consume(&clock)==.004&&sync_step_consume(&clock)==0);
    assert(clock.captures==1&&clock.consumes==1);
    sync_step_capture(&clock,.008);sync_step_reset(&clock);assert(sync_step_consume(&clock)==0);
    sync_step_capture(&clock,NAN);sync_step_capture(&clock,-1);sync_step_capture(&clock,.2);
    assert(sync_step_consume(&clock)==0);
    sync_step_capture(&clock,.002);sync_step_capture(&clock,0);sync_step_capture(&clock,0);
    assert(sync_step_consume(&clock)==.002&&sync_step_consume(&clock)==0);
    ShiftState sync_shift={.paddles=1,.selected=3,.auto_time=0};float sync_effective=1;
    assert(synchronizer_request(&sync_shift,2,3,2,&sync_effective)==3&&sync_effective==0&&sync_shift.auto_time==.16f);
    cancel_rejected_shift(&sync_shift,2);assert(sync_shift.selected==2&&sync_shift.auto_time==0);
    sync_shift.auto_time=.16f;
    sync_effective=.7f;assert(synchronizer_request(&sync_shift,2,3,3,&sync_effective)==3&&sync_effective==.7f);
    sync_shift.paddles=0;assert(synchronizer_request(&sync_shift,4,2,2,&sync_effective)==4);
    assert(grinding_shift(1,0,3,1,1,7,1,850));
    assert(!grinding_shift(0,0,3,1,1,7,1,850));
    assert(!grinding_shift(1,1,3,1,1,7,1,850));
    assert(!grinding_shift(1,0,1,3,1,7,1,850));
    assert(!grinding_shift(1,0,3,3,3,7,1,850));
    assert(!grinding_shift(1,0,3,1,3,7,0,850));
    assert(!grinding_shift(1,0,3,1,1,7,1,0));
    assert(!grinding_shift(1,0,8,1,1,7,1,850));
    assert(!grinding_shift(1,0,3,1,1,7,NAN,850));
    assert(!grinding_shift(1,0,3,1,1,7,1,NAN));
    assert(!starter_message(0,1,1,1));
    assert(!strcmp(starter_message(1,0,1,1),"REALISTIC | ENGINE RUNNING"));
    assert(!strcmp(starter_message(1,1,0,1),"ENGINE STALLED | HOLD IGNITION TO START"));
    assert(!strcmp(starter_message(1,1,1,1),"STARTING | KEEP CLUTCH DOWN + HOLD IGNITION"));
    assert(!strcmp(starter_message(1,1,1,0),"STARTER BLOCKED | CLUTCH FULL DOWN"));
    assert(!strcmp(starter_message(1,1,0,0),"ENGINE STALLED | CLUTCH FULL DOWN + HOLD IGNITION"));
    assert(starter_interlock(1,0)&&starter_interlock(1,.05f));
    assert(!starter_interlock(0,0)&&!starter_interlock(1,.051f));
    assert(!starter_interlock(1,1)&&!starter_interlock(1,NAN));
    assert(!starter_interlock(1,-1)&&!starter_interlock(1,INFINITY));
    assert(starter_torque(-1,1,0)==-1);
    assert(starter_torque(-1,1,1)==.05f);
    assert(starter_torque(0,1,1)==.35f);
    assert(starter_torque(-1,0,1)==-1);
    EngineState restart={.stalled=1};float restart_rpm=0;
    for(int i=0;i<240&&restart.stalled;i++) {
        restart_rpm+=starter_torque(-1,.338972f,1)/.0003335f/120*9.549304f;
        engine_step(&restart,restart_rpm,1000,1.0f/120,1,1);
    }
    assert(!restart.stalled&&restart_rpm>=750);
    assert(engagement(65535,65535,0)==1);
    assert(engagement(0,65535,0)==0);
    assert(fabsf(engagement(32768,65535,0)-0.5f)<0.0001f);
    assert(engagement(0,0,65535)==1 && engagement(65535,0,65535)==0);
    assert(engagement(0,0,255)==1 && engagement(255,0,255)==0);
    assert(fabsf(engagement(128,0,255)-127.0f/255)<.0001f);
    assert(manual_gear(3,2,engagement(0,0,255),10)==2);
    assert(manual_gear(3,2,engagement(255,0,255),10)==3);
    assert(controller_request(2,2,3,7,0,10,0)==3); /* A button edge must survive synchronization. */
    assert(controller_request(3,2,2,7,1,10,0)==2);
    assert(controller_request(3,2,2,7,0,10,0)==3);
    assert(controller_request(-1,2,3,7,0,10,0)==3);
    assert(controller_request(8,2,8,7,0,10,0)==2);
    assert(controller_boundary(-1,1,1,1,0,0)==0);
    assert(controller_boundary(1,0,0,0,0,0)==1);
    assert(controller_boundary(-1,1,1,1,1,0)==-1);
    assert(controller_boundary(-1,1,1,1,0,1)==-1);
    assert(controller_boundary(-1,1,2,1,0,0)==-1);
    assert(controller_boundary(-1,0,0,0,0,0)==-1);
    assert(controller_boundary(1,1,1,1,0,0)==-1);
    /* Input event -> physics commit -> input update: the committed write cannot lose the edge. */
    int pending=controller_event_target(1,1,2,1,1,1,0);
    assert(pending==2); /* Select first with pedal out; solver still requires unloaded dogs. */
    assert(controller_request(1,1,pending,7,1,0,1)==2);
    assert(controller_request(2,1,pending,7,0,0,1)==2);
    assert(controller_event_target(1,2,3,2,2,1,20)==3);
    assert(controller_event_target(-1,2,1,2,2,1,20)==1);
    assert(controller_event_target(-1,1,1,1,1,1,0)==1);
    assert(controller_event_target(-1,1,1,1,1,0,0)==0);
    assert(controller_event_target(1,0,0,0,0,0,0)==1);
    assert(controller_event_target(1,0,0,0,0,0,1)==1); /* Neutral always releases a gear. */
    assert(controller_event_target(1,7,7,7,7,0,20)==7);
    assert(controller_request(3,2,2,7,NAN,20,1)==2);
    assert(controller_request(3,2,2,7,0,NAN,1)==2);
    assert(controller_request(0,2,2,7,1,0,1)==2);
    assert(controller_request(2,0,0,7,0,1,1)==0);
    ManualParams launch={.engine_inertia=.2,.input_inertia=.02,.output_inertia=10,
        .clutch_capacity=250,.synch_capacity=8,.match_rad_s=.5,.max_engagement_load=3,
        .grind_delay=.15,.reverse_max_mps=.1,.ratios={-3.2,0,3.5,2.1,1.4,1,.8,.65}};
    ManualState launch_state={.engine_omega=90,.input_omega=90,.mode=MANUAL_NEUTRAL};
    for(int i=0;i<50;i++) {
        launch_state.engine_omega=90;
        ManualOutput o=manual_step(&launch_state,&launch,(ManualInput){pending-1,1,0,.004});
        assert(o.valid&&launch_state.mode==MANUAL_SYNCHRONIZING);
    }
    for(int i=0;i<100&&launch_state.mode!=MANUAL_ENGAGED;i++) {
        ManualOutput o=manual_step(&launch_state,&launch,(ManualInput){pending-1,0,0,.004});
        assert(o.valid&&o.clutch_torque==0);
    }
    assert(launch_state.mode==MANUAL_ENGAGED&&launch_state.gear==1);
    launch_state.engine_omega=90;
    ManualOutput launch_out=manual_step(&launch_state,&launch,(ManualInput){pending-1,1,0,.004});
    assert(launch_out.valid&&launch_out.output_reaction>0&&launch_out.engine_reaction<0);
    assert(controller_routed_request(0,2)==0);
    assert(controller_routed_request(0,1)==0);
    assert(controller_routed_request(2,0)==2); /* Braking cannot select reverse. */
    assert(controller_routed_request(2,3)==2); /* Shift events are latched separately. */
    assert(controller_routed_request(-1,3)==3);
    for(int bits=0;bits<8;bits++)
        assert(controller_native_steering(bits&1,bits&2,bits&4)==(bits==7));
    assert(controller_event_target(-1,2,1,2,1,1,0)==1); /* Cancel a pending first. */
    assert(engagement(-100,65535,0)==0 && engagement(70000,65535,0)==1);
    assert(engagement(10,10,10)==1);
    ClutchState clutch={0};
    assert(clutch_step(&clutch,1e38f,1,1,.01f,.01f,.05f,.95f,1.25f,1e38f,1)==0);
    assert(clutch_step(&clutch,1e38f,1,1,.01f,.01f,.05f,.95f,1.25f,12,1e38f)==0);
    assert(clutch_step(&clutch,100,0,1,.01f,.01f,.05f,.95f,1.25f,12,1)==0);
    assert(clutch_step(&clutch,NAN,1,1,.01f,.01f,.05f,.95f,1.25f,12,1)==0);
    assert(clutch_step(&clutch,100,1,1,.01f,0,.05f,.95f,1.25f,12,1)==0);
    /* Unforced two-shaft system: kinetic + elastic energy must never grow. */
    for(int rate=30;rate<=240;rate*=2)for(int sign=-1;sign<=1;sign+=2) {
        ClutchState c={0};float dt=1.0f/rate,j=.0002f,slip=sign*300.0f;
        float k=j*powf(6.28318530718f*12,2),energy=.5f*j*slip*slip;
        for(int i=0;i<rate*10;i++) {
            float t=clutch_step(&c,slip,1,.34f,j,dt,.05f,.95f,1.25f,12,1);
            assert(isfinite(t)&&fabsf(t)<=.425001f);
            slip-=dt*t/j;
            float next=.5f*j*slip*slip+.5f*k*c.twist*c.twist;
            assert(next<=energy+1e-5f);energy=next;
        }
        assert(fabsf(slip)<.001f);
        assert(clutch_step(&c,100,0,.34f,j,dt,.05f,.95f,1.25f,12,1)==0&&c.twist==0);
    }
    /* Equal/opposite shaft impulses conserve angular momentum, including backdrive. */
    for(int sign=-1;sign<=1;sign+=2) {
        ClutchState c={0};float e=sign*200,w=sign*50,je=.00033f,jw=.002f,dt=1.0f/120;
        float momentum=je*e+jw*w;
        for(int i=0;i<1200;i++) {
            float t=clutch_step(&c,e-w,1,.34f,1/(1/je+1/jw),dt,.05f,.95f,1.25f,12,1);
            e-=dt*t/je;w+=dt*t/jw;
        }
        assert(fabsf(je*e+jw*w-momentum)<1e-5f&&fabsf(e-w)<.001f);
    }
    /* A coupled clutch carries steady torque without sustained speed mismatch. */
    for(int sign=-1;sign<=1;sign+=2) {
        ClutchState c={0};float e=100,w=100,je=.00033f,jw=.002f,dt=1.0f/120;
        for(int i=0;i<1200;i++) {
            e+=sign*.1f*dt/je;
            float t=clutch_step(&c,e-w,1,.34f,1/(1/je+1/jw),dt,.05f,.95f,1.25f,12,1);
            e-=dt*t/je;w+=dt*t/jw;
        }
        assert(fabsf(e-w)<.03f&&!c.sliding);
        assert(fabsf(c.torque-sign*.1f*jw/(je+jw))<.001f);
    }
    EngineState s={0};
    for(int i=0;i<200;i++)engine_step(&s,1000,1000,.01f,0,1);
    assert(!s.stalled);
    for(int i=0;i<10;i++)engine_step(&s,300,1000,.01f,0,0);
    assert(!s.stalled);
    engine_step(&s,600,1000,.01f,0,0);assert(s.low_time==0);
    for(int i=0;i<20;i++)engine_step(&s,100,1000,.01f,0,0);
    assert(s.stalled);
    engine_step(&s,0,1000,.01f,1,1);assert(s.stalled);
    engine_step(&s,800,1000,.01f,0,1);assert(s.stalled);
    engine_step(&s,800,1000,.01f,1,0);assert(s.stalled);
    engine_step(&s,800,1000,.01f,1,1);assert(!s.stalled);
    s.stalled=1;engine_step(&s,1000,1000,.01f,0,0);assert(!s.stalled);
    s.stalled=1;engine_step(&s,NAN,1000,.01f,1,1);assert(s.stalled);
    assert(idle_assist(-.05f,500,1000,1)==.12f);
    assert(idle_assist(.3f,500,1000,1)==.3f);
    assert(idle_assist(-.05f,1100,1000,1)==-.05f);
    assert(pedal(NAN)==0&&pedal(-1)==0&&pedal(2)==1&&pedal(.4f)==.4f);
    assert(passive_torque(-.1f,-10,1)==0);
    assert(passive_torque(.1f,-10,1)==.1f);
    assert(passive_torque(.1f,10,1)==0);
    assert(passive_torque(-.1f,10,1)==-.1f);
    assert(passive_torque(.1f,0,1)==0);
    assert(passive_torque(-.1f,-10,0)==-.1f);
    for(int i=-100;i<=100;i++)for(int j=-10;j<=10;j++)
        assert(passive_torque(i/100.0f,(float)j,1)*j<=0);
    assert(manual_gear(3,2,1,0)==2);
    assert(manual_gear(3,2,0,10)==3);
    assert(manual_gear(0,2,0,2)==2);
    assert(manual_gear(0,1,0,0)==0);
    assert(manual_gear(2,0,0,2)==0);
    assert(manual_gear(1,2,1,10)==1);
    assert(manual_gear(-1,2,0,0)==2);
    ShiftState sh={.last_h=-1};float effective;
    assert(shift_request(&sh,2,1,0,2,7,1,10,.01f,&effective)==3&&effective==0);
    assert(shift_request(&sh,2,1,0,3,7,1,10,.01f,&effective)==3);
    for(int i=0;i<20;i++)assert(shift_request(&sh,2,0,0,3,7,1,10,.01f,&effective)==3);
    assert(effective==1);
    assert(shift_request(&sh,2,0,1,3,7,1,10,.01f,&effective)==2);
    assert(shift_request(&sh,4,0,0,2,7,1,10,.01f,&effective)==2&&!sh.paddles);
    assert(shift_request(&sh,4,0,0,2,7,0,10,.01f,&effective)==4);
    sh=(ShiftState){.last_h=-1};
    assert(shift_request(&sh,1,0,1,1,7,1,0,.01f,&effective)==0);
    sh=(ShiftState){.last_h=-1};
    assert(shift_request(&sh,1,1,0,1,7,1,0,.01f,&effective)==2&&effective==0);
    sh=(ShiftState){.last_h=-1};
    assert(shift_request(&sh,0,1,0,0,7,1,0,.01f,&effective)==1);
    /* First -> neutral -> reverse requires two released paddle presses. */
    sh=(ShiftState){.last_h=-1};
    assert(shift_request(&sh,2,0,1,2,7,1,0,.01f,&effective)==1);
    assert(shift_request(&sh,2,0,1,1,7,1,0,.01f,&effective)==1);
    shift_request(&sh,2,0,0,1,7,1,0,.01f,&effective);
    assert(shift_request(&sh,2,0,1,1,7,1,0,.01f,&effective)==0);
    /* Moving neutral cannot queue reverse; stopping while held must not engage it. */
    sh=(ShiftState){.last_h=-1};
    assert(shift_request(&sh,1,0,1,1,7,1,2,.01f,&effective)==1);
    assert(shift_request(&sh,1,0,1,1,7,1,0,.01f,&effective)==1);
    shift_request(&sh,1,0,0,1,7,1,0,.01f,&effective);
    assert(shift_request(&sh,1,0,1,1,7,1,0,.01f,&effective)==0);
    assert(shift_request(&sh,1,0,0,1,7,1,2,.01f,&effective)==1);
    assert(shift_request(&sh,1,0,0,1,7,1,0,.01f,&effective)==1);
    sh=(ShiftState){.last_h=-1};
    assert(shift_request(&sh,1,0,1,1,7,1,NAN,.01f,&effective)==1);
    sh=(ShiftState){.last_h=-1};
    assert(shift_request(&sh,2,1,1,2,7,1,0,.01f,&effective)==2);
    WheelButtonState buttons={0};WheelButtonActions action;
    wheel_buttons_step(&buttons,1,1,0,0,0,0,0,0,1000);
    assert(!wheel_buttons_step(&buttons,1,1,1,1,0,0,0,1,1000).toggle);
    assert(!wheel_buttons_step(&buttons,1,1,1,1,0,0,0,1000,1000).toggle);
    assert(wheel_buttons_step(&buttons,1,1,1,1,0,0,0,1001,1000).toggle);
    assert(!wheel_buttons_step(&buttons,1,1,1,1,0,0,0,3000,1000).toggle);
    wheel_buttons_step(&buttons,1,1,0,1,0,0,0,3001,1000);
    assert(!wheel_buttons_step(&buttons,1,1,1,1,0,0,0,5000,1000).toggle);
    wheel_buttons_step(&buttons,1,1,0,0,0,0,0,5001,1000);
    wheel_buttons_step(&buttons,1,1,1,1,0,0,0,5002,1000);
    int enabled=0;enabled^=wheel_buttons_step(&buttons,1,1,1,1,0,0,0,6002,1000).toggle;
    assert(enabled); /* OFF -> ON uses the same independent input state. */
    action=wheel_buttons_step(&buttons,1,1,0,0,1,1,0,6003,1000);
    assert(action.camera&&action.look);
    assert(!wheel_buttons_step(&buttons,1,1,0,0,1,1,0,6004,1000).camera);
    assert(!wheel_buttons_step(&buttons,1,0,0,0,1,1,0,6005,1000).look);
    assert(!wheel_buttons_step(&buttons,1,1,0,0,1,1,0,6006,1000).look);
    wheel_buttons_step(&buttons,1,1,0,0,0,0,0,6007,1000);
    assert(wheel_buttons_step(&buttons,1,1,0,0,0,1,0,6008,1000).look);
    assert(!wheel_buttons_step(&buttons,0,1,0,0,0,1,0,6009,1000).look);
    wheel_buttons_step(&buttons,1,1,0,0,0,0,0,6010,1000);
    action=wheel_buttons_step(&buttons,1,1,0,0,0,1,1,6011,1000);
    assert(action.pause&&!action.look);
    buttons=(WheelButtonState){.armed=1};
    wheel_buttons_step(&buttons,1,1,1,1,0,0,0,UINT32_MAX-500,1000);
    assert(wheel_buttons_step(&buttons,1,1,1,1,0,0,0,499,1000).toggle);
    return 0;
}
#else
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <dinput.h>
#include <xinput.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "bindings-core.h"
#include "../common/runtime-paths.h"

typedef void (__attribute__((thiscall)) *UpdateFn)(void *,float);
typedef HRESULT (WINAPI *CreateFn)(HINSTANCE,DWORD,REFIID,LPVOID *,LPUNKNOWN);
typedef HRESULT (WINAPI *StateFn)(IDirectInputDevice8A *,DWORD,LPVOID);
typedef float (__attribute__((thiscall)) *TorqueFn)(void *);
typedef void (__attribute__((thiscall)) *PostFn)(void *);
typedef int (__cdecl *RestFn)(void *,void *,int);
static RestFn old_rest;
static unsigned neutral_coast_overrides;
static UpdateFn old_update;
static UpdateFn old_distribute;
static TorqueFn old_torque;
static PostFn old_post;
static IDirectInput8A *input;
static IDirectInputDevice8A *device;
static StateFn raw_state;
typedef DWORD (WINAPI *PadStateFn)(DWORD,XINPUT_STATE *);
static PadStateFn pad_state;
/* shortcut: choose pedal source at startup; restart when switching wheel/controller. */
static int controller_mode,source_kind=SOURCE_AUTO,axis_index=1;
static int clutch_key=86,ignition_key=73,toggle_key=VK_F7,wheel_ignition=-1;
static DWORD gamepad_index;
static unsigned pad_clutch=0x100,pad_ignition=0x80,pad_throttle;
static int handbrake_enabled=1;
static const DWORD axis_offsets[]={DIJOFS_X,DIJOFS_Y,DIJOFS_Z,DIJOFS_RX,DIJOFS_RY,DIJOFS_RZ,DIJOFS_SLIDER(0),DIJOFS_SLIDER(1)};
static char realism_path[MAX_PATH];
static int load_bindings(void)
{
    char text[32];
    GetPrivateProfileStringA("Controls","Input","Auto",text,sizeof(text),realism_path);
    source_kind=input_kind(text);
    clutch_key=(int)GetPrivateProfileIntA("Controls","ClutchKey",86,realism_path);
    ignition_key=(int)GetPrivateProfileIntA("Controls","IgnitionKey",73,realism_path);
    toggle_key=(int)GetPrivateProfileIntA("Controls","ToggleKey",VK_F7,realism_path);
    gamepad_index=GetPrivateProfileIntA("Controls","GamepadIndex",0,realism_path);
    wheel_ignition=(int)GetPrivateProfileIntA("Controls","WheelIgnitionButton",0,realism_path)-1;
    GetPrivateProfileStringA("Controls","GamepadClutch","LB",text,sizeof(text),realism_path);
    pad_clutch=pad_binding(text);
    GetPrivateProfileStringA("Controls","GamepadIgnition","RTHUMB",text,sizeof(text),realism_path);
    pad_ignition=pad_binding(text);
    GetPrivateProfileStringA("Controls","GamepadThrottle","Native",text,sizeof(text),realism_path);
    pad_throttle=!strcmp(text,"Native")?0:pad_binding(text);
    return source_kind>=0&&clutch_key>0&&clutch_key<=255&&ignition_key>=0&&ignition_key<=255&&
        toggle_key>=0&&toggle_key<=255&&gamepad_index<XUSER_MAX_COUNT&&wheel_ignition>=-1&&wheel_ignition<128&&
        pad_clutch!=~0u&&pad_clutch!=0&&pad_ignition!=~0u&&pad_clutch!=pad_ignition&&
        pad_throttle!=~0u&&(!pad_throttle||(pad_throttle!=pad_clutch&&pad_throttle!=pad_ignition))&&
        (pad_ignition!=0x10||gamepad_index==0)&&clutch_key!=ignition_key&&
        (!toggle_key||(toggle_key!=clutch_key&&toggle_key!=ignition_key));
}
static HandbrakeState handbrake;
static void *handbrake_owner;
static LONG handbrake_rearm;
typedef int (__cdecl *PauseScanFn)(void *,unsigned *,unsigned,void *,void *);
static PauseScanFn native_pause;
static int start_consumed;
typedef void (__cdecl *ShiftEventFn)(int,int,int,void *);
static ShiftEventFn native_shift_up,native_shift_down;
static unsigned char *wheel;
static long released=65535,pressed=0;
static char ini_path[MAX_PATH],log_path[MAX_PATH];
static DWORD last_log;
static char grind_path[MAX_PATH];
static LONG grind_until;
static int grind_enabled=1,grind_playing,grind_sound_missing;
static void log_status(const char *text);
static unsigned samples;
static float minimum=1,maximum=0;
static void *player_drive;
static unsigned limited_samples;
static float last_demand,last_torque,last_peak;
static void *player_engine;
static EngineState motor;
static int realistic=1,starter,isolated,toggle_was,foreground,cranking,clutch_down;
static float bite_start=.05f,bite_end=.95f;
static char car_section[40]="Car.DEFAULT";
static DWORD last_config;
static int h_buttons[9],paddle_up=-1,paddle_down=-1,hybrid;
static ShiftState shifting={.last_h=-1};
static int stall_enabled=0,paddle_enabled=1,input_lost=1;
static float clutch_release,clutch_strength=1.25f,clutch_frequency=12,clutch_damping=1;
static ClutchState clutch;
static int clutch_gear=-1;
static PostFn old_initial,old_finish;
static unsigned solver_samples;
static int synchronizer_model=0,sync_ready,sync_target=1,sync_event_pending;
static SyncStepClock sync_clock;
static ManualState sync_state;
static ManualOutput sync_result;
static float sync_speed;
static unsigned sync_failures,sync_rejections;
static int sync_last_rejected=-999;
static ClutchEstimates sync_estimates={10,15,50,5,150,10};
static DWORD sync_last_log;
static float __attribute__((thiscall)) engine_torque(void *engine);
static int hooks_ready;

#include "dualsense-native.h"
#include "rumble-native.h"
static void reset_sync(void)
{rumble_invalidate();dualsense_invalidate();sync_step_reset(&dualsense_clock);sync_step_reset(&rumble_clock);sync_ready=0;sync_state=(ManualState){0};sync_result=(ManualOutput){0};sync_target=1;sync_event_pending=0;}
static WheelButtonState wheel_buttons;
static LONG pending_wheel_toggle;
static int buttons_rearm;
static int sample(float *value,DIJOYSTATE2 *s,int *ignition);
static int held(const DIJOYSTATE2 *s,int button);
static void dispatch_buttons(WheelButtonActions actions,int enabled);
static int buttons_enabled=1,combo_enabled=1,combo_a=11,combo_b=10;
static int camera_button=3,look_button=7,pause_button=9;
static DWORD combo_hold=1000,status_until,last_button_update,key_until[3];
static const WORD action_scans[3]={0x2e,0x30,0x01}; /* EA PC manual: C camera, B look back, Escape pause. */
static int key_down[3];
static CRITICAL_SECTION button_lock;
static void synthetic_key(int index,int down)
{
    if(key_down[index]==down)return;
    INPUT event={0};event.type=INPUT_KEYBOARD;event.ki.wScan=action_scans[index];
    event.ki.dwFlags=KEYEVENTF_SCANCODE|(down?0:KEYEVENTF_KEYUP);
    if(SendInput(1,&event,sizeof(event))==1)key_down[index]=down;
}
static VOID CALLBACK release_button_keys(HWND window,UINT message,UINT_PTR timer,DWORD now)
{
    (void)window;(void)message;(void)timer;
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    EnterCriticalSection(&button_lock);
    if(buttons_rearm){wheel_buttons=(WheelButtonState){0};buttons_rearm=0;}
    DIJOYSTATE2 raw={0};float value;
    int valid=sample(&value,&raw,NULL),focused=pid==GetCurrentProcessId();
    if(!valid||!focused)InterlockedExchange(&handbrake_rearm,1);
    WheelButtonActions actions=wheel_buttons_step(&wheel_buttons,valid&&!controller_mode,focused,
        combo_enabled&&held(&raw,combo_a),combo_enabled&&held(&raw,combo_b),
        buttons_enabled&&held(&raw,camera_button),buttons_enabled&&held(&raw,look_button),
        buttons_enabled&&held(&raw,pause_button),now,combo_hold);
    if(actions.toggle)InterlockedXor(&pending_wheel_toggle,1);
    dispatch_buttons(actions,valid&&focused&&buttons_enabled);
    int grind=valid&&focused&&InterlockedCompareExchange(&grind_until,0,0)!=0&&(LONG)((DWORD)InterlockedCompareExchange(&grind_until,0,0)-now)>0;
    if(grind!=grind_playing) {
        if(grind) {
            if(!grind_sound_missing&&!PlaySoundA(grind_path,NULL,SND_FILENAME|SND_ASYNC|SND_LOOP|SND_NODEFAULT)) {
                grind_sound_missing=1;log_status("grind_audio_failed=1");
            }
        } else if(!grind_sound_missing)PlaySoundA(NULL,NULL,0);
        grind_playing=grind;
        log_status(grind?"gear_grind=1 shift_blocked=1":"gear_grind=0");
    }
    for(int i=0;i<3;i++)if(!focused||!valid||(i!=1&&(LONG)(now-key_until[i])>=0))synthetic_key(i,0);
    LeaveCriticalSection(&button_lock);
    rumble_tick(focused);
    dualsense_tick(focused);
}
static void dispatch_buttons(WheelButtonActions actions,int enabled)
{
    EnterCriticalSection(&button_lock);last_button_update=GetTickCount();
    if(!enabled){for(int i=0;i<3;i++)synthetic_key(i,0);}
    else {
        if(actions.camera){synthetic_key(0,1);key_until[0]=last_button_update+80;}
        synthetic_key(1,actions.look);
        if(actions.pause){synthetic_key(2,1);key_until[2]=last_button_update+80;}
    }
    LeaveCriticalSection(&button_lock);
}


__declspec(dllexport) const char *__cdecl NFSU2RealismStatus(void)
{
    if(InterlockedCompareExchange(&grind_until,0,0)!=0&&(LONG)((DWORD)InterlockedCompareExchange(&grind_until,0,0)-GetTickCount())>0)return "GEAR GRIND | SHIFT BLOCKED - PRESS CLUTCH";
    if(status_until&&(LONG)(status_until-GetTickCount())>0)return realistic?"REALISM ON":"REALISM OFF | WHEEL FIX";
    if(!realistic||!player_engine)return NULL;
    if(!stall_enabled)return input_lost?"CLUTCH | INPUT LOST - DRIVE DISCONNECTED":synchronizer_model?"SYNCHRONIZER CLUTCH | STALLING OFF":"SPRING CLUTCH | STALLING OFF";
    if(controller_mode&&motor.stalled)return clutch_down?"ENGINE STALLED | HOLD IGNITION":"ENGINE STALLED | HOLD CLUTCH + IGNITION";
    return starter_message(1,motor.stalled,starter,clutch_down);
}

static void log_status(const char *text)
{FILE *f=fopen(log_path,"a");if(f){fprintf(f,"%s\n",text);fclose(f);}}
static int readable(const void *p,size_t size)
{
    MEMORY_BASIC_INFORMATION m;
    return p && VirtualQuery(p,&m,sizeof(m)) && m.State==MEM_COMMIT &&
        !(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)) &&
        (uintptr_t)p+size<=(uintptr_t)m.BaseAddress+m.RegionSize;
}
#include "wheelfix-native.h"
static int controller_sample(float *value,int *ignition)
{
    XINPUT_STATE state;
    if(!pad_state||pad_state(gamepad_index,&state)!=ERROR_SUCCESS)return 0;
    *value=1-binding_press(pad_clutch,state.Gamepad.wButtons,state.Gamepad.bLeftTrigger,state.Gamepad.bRightTrigger);
    if(ignition)*ignition=binding_press(pad_ignition,state.Gamepad.wButtons,state.Gamepad.bLeftTrigger,state.Gamepad.bRightTrigger)>=.95f;
    return 1;
}
typedef struct {uint32_t type,unused,event;void *scan;uint32_t binding,range,param,key;} Scanner;
_Static_assert(sizeof(Scanner)==32&&offsetof(Scanner,scan)==12,"native scanner ABI");
typedef int (__cdecl *ScanFn)(void *,unsigned *,unsigned,Scanner *,void *);
static int __cdecl scan_throttle(void *node,unsigned *unknown,unsigned flags,Scanner *config,void *joystick)
{
    if(joystick!=(void *)0x8655a0||source_kind!=SOURCE_GAMEPAD||!pad_throttle||
       (config!=(Scanner *)0x7fe190&&config!=(Scanner *)0x7fe1b0))
        return ((ScanFn)0x579150)(node,unknown,flags,config,joystick);
    XINPUT_STATE state={0};
    if(!pad_state||pad_state(gamepad_index,&state)!=ERROR_SUCCESS)memset(&state,0,sizeof(state));
    return (int)throttle_event(config->event,pad_throttle,state.Gamepad.wButtons,
        state.Gamepad.bLeftTrigger,state.Gamepad.bRightTrigger);
}
static int sample(float *value,DIJOYSTATE2 *s,int *ignition)
{
    if(ignition)*ignition=0;
    if(source_kind==SOURCE_GAMEPAD)return controller_sample(value,ignition);
    if(source_kind==SOURCE_KEYBOARD){*value=(GetAsyncKeyState(clutch_key)&0x8000)?0:1;return 1;}
    if(!device)return 0;
    EnterCriticalSection(&button_lock);
    HRESULT hr=IDirectInputDevice8_Poll(device);
    if(SUCCEEDED(hr))hr=raw_state(device,sizeof(*s),s);
    if(FAILED(hr))IDirectInputDevice8_Acquire(device);
    else {
        LONG axis;memcpy(&axis,(char *)s+axis_offsets[axis_index],sizeof(axis));
        *value=engagement(axis,released,pressed);
        if(ignition)*ignition=held(s,wheel_ignition);
    }
    LeaveCriticalSection(&button_lock);
    return SUCCEEDED(hr);
}
static int __cdecl pause_scan(void *node,unsigned *unknown,unsigned flags,void *config,void *joystick)
{
    int result=native_pause(node,unknown,flags,config,joystick);
    if(source_kind!=SOURCE_GAMEPAD||pad_ignition!=0x10||gamepad_index!=0||joystick!=(void *)0x8655a0)return result;
    void *self=*(void **)(wheel+0x61300);
    if(!readable(self,0x2c4)||!stall_enabled||
       !clutch_physics_eligible(realistic,*(int *)((char *)self+0x2c0),*(unsigned char *)((char *)self+0x2b4))){
        start_consumed=0;return result;
    }
    XINPUT_STATE state={0};
    if(pad_state(0,&state)!=ERROR_SUCCESS)memset(&state,0,sizeof(state));
    float press=binding_press(pad_clutch,state.Gamepad.wButtons,state.Gamepad.bLeftTrigger,state.Gamepad.bRightTrigger);
    int ignition=binding_press(pad_ignition,state.Gamepad.wButtons,state.Gamepad.bLeftTrigger,state.Gamepad.bRightTrigger)>=.95f;
    return consume_ignition_start(&start_consumed,1,ignition,press>=.95f,(state.Gamepad.wButtons&0x10)!=0)?0:result;
}
static void controller_shift(ShiftEventFn original,int direction,int device_id,int event,int value,void *self)
{
    int player=controller_mode&&self==*(void **)(wheel+0x61300);
    int requested=player?*(int *)((char *)self+0x1e4):-1;
    int pending=synchronizer_model?sync_target:requested;
    int eligible=player&&clutch_physics_eligible(realistic,*(int *)((char *)self+0x2c0),
        *(unsigned char *)((char *)self+0x2b4));
    /* Native +/- navigation must start from the selected lever, not a temporary neutral. */
    if(synchronizer_model&&eligible&&(value&255)&&pending>=0&&pending<=7){
        requested=pending;*(int *)((char *)self+0x1e4)=pending;
    }
    original(device_id,event,value,self);
    if(!eligible||!(value&255))return;
    void *car=*(void **)((char *)self+0x2a8);
    float foot;
    DIJOYSTATE2 raw={0};
    if(!readable(car,0x43c)||!sample(&foot,&raw,NULL))return;
    int actual=*(int *)((char *)car+0x434),after=*(int *)((char *)self+0x1e4);
    int target=synchronizer_model?controller_event_target(direction,requested,after,pending,actual,foot,*(float *)((char *)car+0x42c)):
        controller_boundary(direction,requested,pending,actual,foot,*(float *)((char *)car+0x42c));
    if(target<0)return;
    if(synchronizer_model){sync_target=target;sync_event_pending=1;}
    else *(int *)((char *)self+0x1e4)=target;
    if(target==pending&&target==requested)return;
    char text[160];snprintf(text,sizeof(text),"controller_shift_edge=%d from=%d native_after=%d target=%d clutch_release=%.4f",direction,requested,after,target,foot);
    log_status(text);
}
static void __cdecl shift_up(int device_id,int event,int value,void *self)
{controller_shift(native_shift_up,1,device_id,event,value,self);}
static void __cdecl shift_down(int device_id,int event,int value,void *self)
{controller_shift(native_shift_down,-1,device_id,event,value,self);}
static int held(const DIJOYSTATE2 *s,int button)
{return button>=0&&button<128&&(s->rgbButtons[button]&0x80)!=0;}
static int auxiliary_button(int button)
{
    /* Do not synthesize keys from buttons used by the drivetrain. */
    if(button<0||button>=128||button==wheel_ignition||
       button==paddle_up||button==paddle_down)return -1;
    for(int i=0;i<9;i++)if(button==h_buttons[i])return -1;
    return button;
}
static int h_slot(const DIJOYSTATE2 *s)
{
    int result=1,count=0;
    for(int gear=0;gear<9;gear++)if(held(s,h_buttons[gear])){result=gear;count++;}
    return count>1?1:result;
}
static float engine_peak(void *engine)
{
    if(!readable(engine,0x94))return 0;
    int count=*(int *)((char *)engine+0x24);
    float *points=*(float **)((char *)engine+0x34);
    float scale=*(float *)((char *)engine+0x90),peak=0;
    if(count<2||count>128||!readable(points,count*sizeof(float))||!isfinite(scale)||scale<=0)return 0;
    for(int i=0;i<count;i++) {
        if(!isfinite(points[i]))return 0;
        if(points[i]>peak)peak=points[i];
    }
    return peak*scale;
}
static void *current_drive(void)
{
    void *self=wheel?*(void **)(wheel+0x61300):NULL;
    if(!readable(self,0x2c4))return NULL;
    void *car=*(void **)((char *)self+0x2a8);
    void *mover=readable(car,0x38)?*(void **)((char *)car+0x34):NULL;
    return readable(mover,0x50)?*(void **)((char *)mover+0x4c):NULL;
}
static int custom_drive(void *drive)
{
    if(!hooks_ready||!realistic||!drive||drive!=player_drive||!readable(drive,0xf4)||current_drive()!=drive)return 0;
    void *self=*(void **)(wheel+0x61300);
    return clutch_physics_eligible(realistic,*(int *)((char *)self+0x2c0),
        *(unsigned char *)((char *)self+0x2b4))&&*(void **)((char *)drive+0x20)==player_engine;
}
static int __cdecl classify_rest(void *car,void *body,int previous)
{
    int result=old_rest(car,body,previous);
    if((result!=1&&result!=2)||!foreground||input_lost||!custom_drive(player_drive))return result;
    void *self=*(void **)(wheel+0x61300);
    if(!readable(self,0x2c4)||!readable(car,0x970)||*(void **)((char *)self+0x2a8)!=car||
       *(int *)((char *)car+0x28)!=1||*(int *)((char *)car+0x96c)||
       *(unsigned char *)((char *)self+0x2b4)||*(int *)((char *)self+0x2b8))return result;
    float hold=*(float *)((char *)self+0x2bc),dt=*(float *)0x7fc800;
    if(!isfinite(hold)||hold>0||!isfinite(dt)||dt<=0||dt>.1f)return result;
    void *mover=*(void **)((char *)car+0x34);
    if(!readable(mover,0x60)||*(void **)((char *)mover+4)!=self||
       *(void **)((char *)mover+0x20)!=body||*(void **)((char *)mover+0x4c)!=player_drive||
       *(void **)((char *)mover+0x5c)!=car)return result;
    int committed=*(int *)((char *)player_drive+0x3c);
    int requested=synchronizer_model?sync_target:*(int *)((char *)player_drive+0x38);
    int eligible=*(int *)((char *)car+0x434)==1&&
        (!synchronizer_model||(sync_ready&&sync_state.mode==MANUAL_NEUTRAL));
    float brake=*(float *)((char *)self+0x214),hand=*(float *)((char *)self+0x210);
    if(!clutch_neutral_coast(result,eligible,committed,requested,brake,hand))return result;
    if(++neutral_coast_overrides==1)log_status("neutral_coast_override=1 native_rest_wake=1 native_gravity_and_brakes_preserved=1");
    /* ApplyMoverRest must run its awake branch to restore gravity after a previous sleep. */
    return 0;
}
/* Exact Wheel Fix build only. Capture/masking remains native; only its gear writer is gated. */
typedef void (__cdecl *ShifterUpdateFn)(const GUID *,const DIJOYSTATE *,DWORD);
static ShifterUpdateFn old_shifter_update;
static CRITICAL_SECTION shifter_lock;
static int shifter_owned;
static int shifter_storage_guard(void)
{
    const unsigned offsets[]={0x433a0,0x612b8,0x612f8,0x61300,0x61360};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        MEMORY_BASIC_INFORMATION region;unsigned char *p=wheel+offsets[i];
        if(!VirtualQuery(p,&region,sizeof(region))||region.State!=MEM_COMMIT||
           (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
           !((region.Protect&0xff)==PAGE_READWRITE||(region.Protect&0xff)==PAGE_WRITECOPY||
             (region.Protect&0xff)==PAGE_EXECUTE_READWRITE||(region.Protect&0xff)==PAGE_EXECUTE_WRITECOPY)||
           (uintptr_t)(p+8)>(uintptr_t)region.BaseAddress+region.RegionSize)return 0;
    }
    return 1;
}
static void __cdecl shifter_update(const GUID *guid,const DIJOYSTATE *state,DWORD size)
{
    EnterCriticalSection(&shifter_lock);
    int owns=synchronizer_model&&hybrid&&!controller_mode&&!wheel[0x433c7]&&custom_drive(player_drive);
    int action=clutch_proxy_action(&shifter_owned,owns);
    if(action==CLUTCH_PROXY_REARM) {
        /* -100 is native startup and bypasses the clutch gate. -2 is lever neutral.
           Clear stale waiting, then let the current captured lever start a fresh native gate. */
        *(int *)(wheel+0x433a0)=-2;
        *(DWORD *)(wheel+0x61360)=0;
        wheel[0x61364]=0;
        log_status("shifter_owner=native pending_wait_cleared=1 rearm=neutral");
    }
    if(action!=CLUTCH_PROXY_SKIP)old_shifter_update(guid,state,size);
    LeaveCriticalSection(&shifter_lock);
}
// Scoped tyre cap ownership shares the existing native coupling hooks.
static float reduced_inertia(void *drive,void *engine)
{
    void *es=*(void **)((char *)engine+0x18),*ds=*(void **)((char *)drive+0x18);
    int gear=*(int *)((char *)drive+0x3c);
    if(!readable(es,0x10)||!readable(ds,0x44)||gear<0||gear>8||gear==1)return 0;
    float j=*(float *)((char *)es+0xc),ratio=*(float *)((char *)ds+0x20+4*gear);
    float rear=*(float *)((char *)drive+0xec);
    if(!isfinite(j)||j<=0||!isfinite(ratio)||!isfinite(rear)||rear<0||rear>1)return 0;
    float inverse=1/j;
    /* Match U2's axle weighting; wheel inertias are reflected through the current ratios. */
    for(int axle=0;axle<2;axle++) {
        float share=axle?rear:1-rear;
        if(share<=0)continue;
        float final=*(float *)((char *)ds+8+4*axle);
        for(int side=0;side<2;side++) {
            void *w=*(void **)((char *)drive+0x24+8*axle+4*side);
            if(!readable(w,0x30)||!isfinite(final))return 0;
            float wi=*(float *)((char *)w+0x2c);
            if(!isfinite(wi)||wi<=0)return 0;
            inverse+=share*ratio*ratio*final*final*.25f/wi;
        }
    }
    return isfinite(inverse)&&inverse>0?1/inverse:0;
}
static void target_rejected(int requested,ClutchTarget choice)
{
    cancel_rejected_shift(&shifting,choice.target);
    if(sync_last_rejected!=requested){
        sync_rejections++;sync_last_rejected=requested;
        char text[160];snprintf(text,sizeof(text),"sync_request_rejected=%d reason=%s retained=%d rejections=%u",
            requested,choice.reason==1?"out_of_range":"missing_ratio",choice.target,sync_rejections);log_status(text);
    }
}
static void load_clutch_estimates(void)
{
    ClutchEstimates next={
        (int)GetPrivateProfileIntA("Clutch","InputInertiaPercent",10,realism_path),
        (int)GetPrivateProfileIntA("Clutch","SynchronizerCapacityPercent",15,realism_path),
        (int)GetPrivateProfileIntA("Clutch","MatchRPM",50,realism_path),
        (int)GetPrivateProfileIntA("Clutch","EngagementLoadPercent",5,realism_path),
        (int)GetPrivateProfileIntA("Clutch","GrindDelayMs",150,realism_path),
        (int)GetPrivateProfileIntA("Clutch","ReverseMaxCmS",10,realism_path)};
    next=clutch_estimates_validate(next);
    if(!clutch_estimates_equal(next,sync_estimates)){sync_estimates=next;reset_sync();}
}
static void sync_failure(void)
{
    rumble_invalidate();
    dualsense_invalidate();
    sync_failures++;sync_ready=0;sync_result=(ManualOutput){0};
    if(GetTickCount()-sync_last_log>1000){log_status("model=sync adapter_valid=0 torque=0 state_reset=1");sync_last_log=GetTickCount();}
}
static void synchronize_drive(void *drive,void *engine)
{
    double dt=sync_step_consume(&sync_clock);
    if(dt==0)return; /* Only one force preparation per observed real physics step. */
    if(input_lost){reset_sync();return;}
    if(!readable(engine,0x94)){sync_failure();return;}
    void *spec=*(void **)((char *)drive+0x18),*es=*(void **)((char *)engine+0x18);
    void *aux=*(void **)((char *)drive+0x34),*wheels[4];
    void *self=*(void **)(wheel+0x61300);
    if(!readable(spec,0x40)||!readable(es,0x10)||!readable(aux,0x430)||!readable(self,0x2c4)){sync_failure();return;}
    NativeWheels w={0};double coeff[4],output_omega,output_inertia,output_accel;
    w.front_final=*(float *)((char *)spec+8);w.rear_final=*(float *)((char *)spec+12);
    w.rear_share=*(float *)((char *)drive+0xec);w.threshold=*(float *)0x7fbffc;
    w.mode=*(int *)((char *)aux+0x42c);
    for(int i=0;i<4;i++){
        wheels[i]=*(void **)((char *)drive+0x24+4*i);
        if(!readable(wheels[i],0x120)){sync_failure();return;}
        w.omega[i]=*(float *)((char *)wheels[i]+0x28);w.inertia[i]=*(float *)((char *)wheels[i]+0x2c);
        w.torque[i]=*(float *)((char *)wheels[i]+0x108);
        w.disabled[i]=*(int *)((char *)wheels[i]+0x10c)!=0;w.contact[i]=*(unsigned char *)((char *)wheels[i]+0x11c)!=0;
    }
    if(!native_coefficients(w,coeff,&output_omega,&output_inertia,&output_accel)){sync_failure();return;}
    double peak=engine_peak(engine),je=*(float *)((char *)es+0xc);
    ManualParams p={.engine_inertia=je,.input_inertia=je*sync_estimates.input_percent/100.0,.output_inertia=output_inertia,
        .clutch_capacity=peak*clutch_strength,.synch_capacity=peak*sync_estimates.capacity_percent/100.0,.match_rad_s=sync_estimates.match_rpm/9.549304,
        .max_engagement_load=peak*sync_estimates.load_percent/100.0,.grind_delay=sync_estimates.grind_ms/1000.0,.reverse_max_mps=sync_estimates.reverse_cms/100.0};
    for(int i=0;i<8;i++)p.ratios[i]=*(float *)((char *)spec+0x20+4*i);
    int actual=*(int *)((char *)drive+0x3c);
    double engine_omega=*(float *)((char *)engine+0x14),feedback=*(float *)((char *)engine+0x7c);
    if(!manual_params_valid(&p)||actual<0||actual>7||!manual_gear_supported(&p,actual-1)||
        !isfinite(dt)||dt<1e-6||dt>.1||!isfinite(engine_omega)||!isfinite(feedback)){sync_failure();return;}
    ClutchTarget choice=clutch_choose_target(sync_target,actual,clutch_gear_mask(p.ratios));
    if(choice.reason){target_rejected(sync_target,choice);sync_target=choice.target;}
    if(sync_ready&&actual!=(sync_state.mode==MANUAL_ENGAGED?sync_state.gear+1:1))sync_ready=0;
    if(!sync_ready){
        sync_state=(ManualState){.input_omega=actual==1?engine_omega:p.ratios[actual]*output_omega,
            .mode=actual==1?MANUAL_NEUTRAL:MANUAL_ENGAGED,.gear=actual-1,.target=actual-1};sync_ready=1;
    }
    /* Engine derivative is (ComputeDriveValue + feedback7C)/spec inertia.
       Predict once, then add reactions to native accumulators; never write speeds. */
    double external=engine_torque(engine)+feedback;
    sync_state.engine_omega=engine_omega+dt*external/je;
    sync_state.output_omega=output_omega+dt*output_accel;
    double release=pedal((clutch_release-bite_start)/(bite_end-bite_start));
    ManualState next=sync_state;
    ManualOutput out=manual_step(&next,&p,(ManualInput){sync_target-1,release,sync_speed,dt});
    double added[4];
    for(int i=0;i<4;i++)added[i]=w.torque[i]+coeff[i]*out.output_reaction;
    int valid=out.valid&&fabs(out.output_reaction)<=peak*100&&isfinite(feedback+out.engine_reaction)&&fabs(feedback+out.engine_reaction)<1e30;
    for(int i=0;i<4;i++)if(!isfinite(added[i])||fabs(added[i])>1e30||fabs(coeff[i]*out.output_reaction)>peak*500)valid=0;
    if(!valid){sync_failure();return;}
    sync_state=next;sync_result=out;
    *(float *)((char *)engine+0x7c)=(float)(feedback+out.engine_reaction);
    for(int i=0;i<4;i++)if(!w.disabled[i])*(float *)((char *)wheels[i]+0x108)=(float)added[i];
    *(float *)((char *)drive+0x70)=(float)-(out.dog_torque+out.synch_torque);
    int committed=next.mode==MANUAL_ENGAGED?next.gear+1:1;
    *(int *)((char *)drive+0x3c)=committed;*(int *)((char *)drive+0x38)=committed;
    *(int *)((char *)drive+0x48)=0;*(int *)((char *)self+0x1e4)=committed;
    if(grind_enabled&&out.grinding)InterlockedExchange(&grind_until,(LONG)(GetTickCount()+150));
    last_demand=(float)(next.engine_omega-next.input_omega);last_torque=(float)out.clutch_torque;last_peak=(float)peak;solver_samples++;
    if(GetTickCount()-sync_last_log>1000){
        char msg[420];snprintf(msg,sizeof(msg),"model=sync mode=%d target=%d committed=%d input_rpm=%.2f mismatch_rpm=%.2f clutch_torque=%.7f sync_torque=%.7f output_torque=%.7f grinding=%d reverse_blocked=%d failures=%u",
            next.mode,sync_target,committed,next.input_omega*9.549304,(next.input_omega-p.ratios[sync_target]*next.output_omega)*9.549304,
            out.clutch_torque,out.synch_torque,out.output_reaction,out.grinding,out.reverse_blocked,sync_failures);
        log_status(msg);sync_last_log=GetTickCount();
    }
}
static void __attribute__((thiscall)) initial_torque(void *drive)
{
    if(!custom_drive(drive)){old_initial(drive);return;}
    *(float *)((char *)drive+0x70)=0;
    /* Mode 1 disables native rigid locking AND its artificial slipping-engine inertia.
       Gear admission is handled by the physical clutch interlock in update(). */
    *(int *)((char *)drive+0x44)=1;
    void *engine=*(void **)((char *)drive+0x20);
    if(synchronizer_model){synchronize_drive(drive,engine);return;}
    int gear=*(int *)((char *)drive+0x3c);
    if(gear!=clutch_gear){clutch=(ClutchState){0};clutch_gear=gear;}
    if(gear==1||!readable(engine,0x94)) {clutch=(ClutchState){0};return;}
    float shaft=((TorqueFn)0x5941d0)(drive);
    float slip=*(float *)((char *)engine+0x14)-shaft;
    float peak=engine_peak(engine),j=reduced_inertia(drive,engine);
    float torque=clutch_step(&clutch,slip,clutch_release,peak,j,*(float *)0x7fc800,
                            bite_start,bite_end,clutch_strength,clutch_frequency,clutch_damping);
    torque=passive_torque(torque,shaft,stall_enabled&&motor.stalled);
    float reaction=*(float *)((char *)engine+0x7c);
    old_distribute(drive,torque);
    /* Native distribution scales engine reaction by driven-wheel contact count. */
    *(float *)((char *)engine+0x7c)=reaction-torque;
    last_demand=slip;last_torque=torque;last_peak=peak;
    solver_samples++;if(clutch.sliding)limited_samples++;
}
static void __attribute__((thiscall)) finish_coupling(void *drive)
{
    /* Native finish can snap wheel speeds and add first-gear reaction torque. */
    if(!custom_drive(drive))old_finish(drive);
    rumble_publish_drive(drive);
    dualsense_publish_drive(drive);
}
static float __attribute__((thiscall)) engine_torque(void *engine)
{
    if(!stall_enabled||!custom_drive(player_drive)||engine!=player_engine)return old_torque(engine);
    float rpm=*(float *)((char *)engine+0x14)*9.549304f;
    void *spec=*(void **)((char *)engine+0x18);
    if(!readable(spec,0xc))return old_torque(engine);
    float idle=*(float *)spec,peak=engine_peak(engine);
    if(motor.stalled) {
        float saved=*(float *)((char *)engine+0x78);
        *(float *)((char *)engine+0x78)=0;
        float drag=rpm>0?old_torque(engine):0;
        *(float *)((char *)engine+0x78)=saved;
        return starter_torque(drag,peak,cranking);
    }
    /* A bounded idle controller replaces U2's unconditional minimum-RPM clamp. */
    return idle_assist(old_torque(engine),rpm,idle,peak);
}
static void __attribute__((thiscall)) engine_post(void *engine)
{
    rumble_capture(engine,*(float *)0x7fc800);
    dualsense_capture(engine,*(float *)0x7fc800);
    if(synchronizer_model&&custom_drive(player_drive)&&engine==player_engine)
        sync_step_capture(&sync_clock,*(float *)0x7fc800);
    if(!stall_enabled||!custom_drive(player_drive)||engine!=player_engine){old_post(engine);return;}
    float before=*(float *)((char *)engine+0x14);
    void *spec=*(void **)((char *)engine+0x18);
    if(!readable(spec,0xc)||!isfinite(before)){old_post(engine);return;}
    float idle=*(float *)spec,maximum=*(float *)((char *)spec+8)/9.549304f;
    if(!isfinite(idle)||idle<=0||!isfinite(maximum)||maximum<=0){old_post(engine);return;}
    old_post(engine);
    if(before<0)before=0;
    if(before>maximum)before=maximum;
    *(float *)((char *)engine+0x14)=before;
    int was=motor.stalled;
    engine_step(&motor,before*9.549304f,idle,*(float *)0x7fc800,cranking,isolated);
    if(was!=motor.stalled)log_status(motor.stalled?"engine_stalled=1 combustion=off":cranking?"engine_started=1 reason=starter combustion=on":"engine_started=1 reason=bump_start combustion=on");
}
static void load_button_config(int startup)
{
    EnterCriticalSection(&button_lock);
    int old_buttons[]={buttons_enabled,combo_enabled,combo_a,combo_b,camera_button,look_button,pause_button,(int)combo_hold};
    buttons_enabled=GetPrivateProfileIntA("WheelButtons","Enabled",0,realism_path)!=0;
    combo_enabled=GetPrivateProfileIntA("Controls","WheelToggleEnabled",0,realism_path)!=0;
    combo_a=GetPrivateProfileIntA("Controls","ToggleButton1",12,realism_path)-1;
    combo_b=GetPrivateProfileIntA("Controls","ToggleButton2",11,realism_path)-1;
    combo_hold=GetPrivateProfileIntA("Controls","ToggleHoldMs",1000,realism_path);
    if(combo_hold<500||combo_hold>5000)combo_hold=1000;
    if(combo_a==combo_b)combo_enabled=0;
    camera_button=GetPrivateProfileIntA("WheelButtons","CameraButton",4,realism_path)-1;
    look_button=GetPrivateProfileIntA("WheelButtons","LookBackButton",8,realism_path)-1;
    pause_button=GetPrivateProfileIntA("WheelButtons","PauseButton",10,realism_path)-1;
    combo_a=auxiliary_button(combo_a);combo_b=auxiliary_button(combo_b);
    if(combo_a<0||combo_b<0)combo_enabled=0;
    camera_button=auxiliary_button(camera_button);look_button=auxiliary_button(look_button);pause_button=auxiliary_button(pause_button);
    int *actions[]={&camera_button,&look_button,&pause_button};
    for(int i=0;i<3;i++)if(combo_enabled&&(*actions[i]==combo_a||*actions[i]==combo_b))*actions[i]=-1;
    if(camera_button>=0&&(camera_button==look_button||camera_button==pause_button))camera_button=-1;
    if(look_button>=0&&look_button==pause_button)look_button=-1;
    int new_buttons[]={buttons_enabled,combo_enabled,combo_a,combo_b,camera_button,look_button,pause_button,(int)combo_hold};
    if(memcmp(old_buttons,new_buttons,sizeof(old_buttons)))buttons_rearm=1;
    LeaveCriticalSection(&button_lock);
    if(startup) {
        char text[256];
        snprintf(text,sizeof(text),"wheel_buttons_enabled=%d camera_button=%d lookback_button=%d pause_button=%d wheel_toggle_enabled=%d toggle_buttons=%d,%d hold_ms=%lu numbering=one_based",
            buttons_enabled,camera_button+1,look_button+1,pause_button+1,combo_enabled,combo_a+1,combo_b+1,(unsigned long)combo_hold);
        log_status(text);
    }
}
static void controls(void)
{
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    foreground=pid==GetCurrentProcessId();
    int down=foreground&&toggle_key&&(GetAsyncKeyState(toggle_key)&0x8000)!=0;
    int wanted=realistic;
    int wheel_toggle=InterlockedExchange(&pending_wheel_toggle,0);
    if((down&&!toggle_was)||wheel_toggle) {
        wanted=!wanted;
        WritePrivateProfileStringA("Realism","Enabled",wanted?"1":"0",realism_path);
    }
    toggle_was=down;
    starter=foreground&&ignition_key&&(GetAsyncKeyState(ignition_key)&0x8000)!=0;
    if(GetTickCount()-last_config>500) {
        wanted=GetPrivateProfileIntA("Realism","Enabled",1,realism_path)!=0;
        float old_bite_start=bite_start,old_bite_end=bite_end;
        float old_strength=clutch_strength,old_frequency=clutch_frequency,old_damping=clutch_damping;
        CarClutchProfile defaults=car_clutch_resolve((CarClutchProfile){
            GetPrivateProfileIntA("Car.DEFAULT","BiteStartPercent",5,realism_path),
            GetPrivateProfileIntA("Car.DEFAULT","BiteEndPercent",95,realism_path),
            GetPrivateProfileIntA("Car.DEFAULT","CapacityPercent",
                GetPrivateProfileIntA("Clutch","CapacityPercent",125,realism_path),realism_path)},car_clutch_defaults());
        CarClutchProfile profile=car_clutch_resolve((CarClutchProfile){
            GetPrivateProfileIntA(car_section,"BiteStartPercent",defaults.bite_start_percent,realism_path),
            GetPrivateProfileIntA(car_section,"BiteEndPercent",defaults.bite_end_percent,realism_path),
            GetPrivateProfileIntA(car_section,"CapacityPercent",defaults.capacity_percent,realism_path)},defaults);
        bite_start=profile.bite_start_percent/100.0f;bite_end=profile.bite_end_percent/100.0f;
        grind_enabled=GetPrivateProfileIntA("Effects","GearGrinding",1,realism_path)!=0;
        int previous_stall=stall_enabled;
        stall_enabled=GetPrivateProfileIntA("Effects","Stalling",0,realism_path)!=0;
        int previous_paddle=paddle_enabled;
        paddle_enabled=GetPrivateProfileIntA("Effects","PaddleAutoClutch",1,realism_path)!=0;
        if(previous_paddle!=paddle_enabled)shifting=(ShiftState){.last_h=-1};
        if(previous_stall!=stall_enabled)memset(&motor,0,sizeof(motor));
        load_clutch_estimates();
        int next_sync=GetPrivateProfileIntA("Clutch","SynchronizerModel",0,realism_path)!=0;
        if(next_sync!=synchronizer_model){reset_sync();clutch=(ClutchState){0};synchronizer_model=next_sync;}
        int frequency=GetPrivateProfileIntA("Clutch","FrequencyHz",12,realism_path);
        int damping=GetPrivateProfileIntA("Clutch","DampingPercent",100,realism_path);
        clutch_strength=profile.capacity_percent/100.0f;
        clutch_frequency=(frequency>=2&&frequency<=30?frequency:12);
        clutch_damping=(damping>=10&&damping<=200?damping:100)/100.0f;
        if(old_bite_start!=bite_start||old_bite_end!=bite_end||old_strength!=clutch_strength||old_frequency!=clutch_frequency||old_damping!=clutch_damping){
            clutch=(ClutchState){0};
            char text[192];snprintf(text,sizeof(text),"clutch_profile=%s bite_release=%.2f..%.2f capacity_peak_factor=%.2f calibration=authored native_inertia_torque=preserved",car_section,bite_start,bite_end,clutch_strength);log_status(text);
        }
        handbrake_enabled=GetPrivateProfileIntA("Handbrake","Enabled",1,realism_path)!=0;
        load_button_config(0);
        rumble_configure();
        dualsense_configure();
        last_config=GetTickCount();
    }
    if(wanted!=realistic) {
        memset(&motor,0,sizeof(motor));
        shifting=(ShiftState){.last_h=-1};clutch=(ClutchState){0};input_lost=1;
        reset_sync();realistic=wanted;status_until=GetTickCount()+4000;
        log_status(realistic?(synchronizer_model?"realistic_mode=on model=synchronizer":"realistic_mode=on model=spring_damper"):"realistic_mode=off native_handoff_requested=1");
    }
}
static void __attribute__((thiscall)) update(void *self,float dt)
{
    int *mode=(int *)((char *)self+0x2c0);
    float value=0;DIJOYSTATE2 raw={0};
    int player=wheel && self==*(void **)(wheel+0x61300);
    void *owned=player?current_drive():NULL;
    int ignition_request=0;
    int raw_valid=player&&sample(&value,&raw,&ignition_request);
    if(player)controls();
    int active=player&&(hybrid||controller_mode)&&clutch_physics_eligible(realistic,*mode,
        *(unsigned char *)((char *)self+0x2b4));
    int valid=active&&raw_valid;
    int recovered=valid&&input_lost;
    float foot=valid?value:0;
    if(player) {
        if(!active&&owned==player_drive&&readable(owned,0x64)) {
            int actual=*(int *)((char *)owned+0x3c);
            *(int *)((char *)owned+0x38)=actual;
            *(int *)((char *)owned+0x48)=0;
            *(int *)((char *)owned+0x60)=*mode;
            *(int *)((char *)owned+0x44)=3;
            *(float *)((char *)owned+0x14)=1;
            *(int *)((char *)self+0x1e4)=(*mode==1||*mode==3)?actual:-1;
        }
        if(owned!=player_drive){reset_sync();clutch=(ClutchState){0};clutch_gear=-1;shifting=(ShiftState){.last_h=-1};recovered=valid;}
        if(valid&&stall_enabled)starter|=foreground&&ignition_request;
        else starter=0;
        clutch_down=starter_interlock(valid,foot);
        cranking=stall_enabled&&starter_interlock(starter,foot);
        if(active&&!valid) {reset_sync();clutch=(ClutchState){0};shifting=(ShiftState){.last_h=-1};}
        input_lost=!valid;
    }
    float gas=valid?pedal(*(float *)((char *)self+0x2d4)):0;
    float brake=valid?pedal(*(float *)((char *)self+0x2dc)):0;
    /* Native pedal routing replaces reverse requests with forward gear. */
    int controller_gear=player&&controller_mode?*(int *)((char *)self+0x1e4):-1;
    if(player&&controller_native_steering(source_kind==SOURCE_GAMEPAD,hooks_ready&&steering_supported,wheel[0x612be])){
        /* The wheel's direct-angle path bypasses the game's controller deadzone. */
        wheel[0x612bf]=0;MemoryBarrier();
        InterlockedExchange((LONG *)(wheel+0x61304),0);
        InterlockedExchange((LONG *)(wheel+0x612f8),0);
        static int logged;
        if(!logged){log_status("controller_native_steering=1 direct_wheel_angle=disabled native_deadzone=preserved");logged=1;}
    }
    float raw_handbrake=0,applied_handbrake=0;
    int handbrake_applied=0;
    if(player&&handbrake_enabled){
        int rearm=InterlockedExchange(&handbrake_rearm,0);
        if(handbrake_owner!=self||rearm)handbrake=(HandbrakeState){0};
        handbrake_owner=self;
        void *car=*(void **)((char *)self+0x2a8);
        handbrake_applied=1;
        raw_handbrake=*(float *)((char *)self+0x2ec);
        int usable=raw_valid&&foreground&&isfinite(raw_handbrake)&&
            readable(car,0x2c)&&*(int *)((char *)car+0x28)==1&&
            !*(unsigned char *)((char *)self+0x2b4)&&!*(int *)((char *)self+0x2b8);
        applied_handbrake=(float)handbrake_step(&handbrake,usable,raw_handbrake>.5f,GetTickCount());
        /* Feed native braking and steering, then restore the mapped button value. */
        *(float *)((char *)self+0x2ec)=applied_handbrake;
    }
    old_update(self,dt);
    if(handbrake_applied&&*(float *)((char *)self+0x2ec)==applied_handbrake)
        *(float *)((char *)self+0x2ec)=raw_handbrake;
    if(active) {
        /* Wheel Fix tests mode 1 while routing reverse. Apply analog coupling after its input path. */
        void *car=*(void **)((char *)self+0x2a8);
        void *mover=readable(car,0x38)?*(void **)((char *)car+0x34):NULL;
        void *drive=readable(mover,0x50)?*(void **)((char *)mover+0x4c):NULL;
        if(readable(drive,0x64)&&*(void **)((char *)drive+0x20)!=player_engine) {
            shifting=(ShiftState){.last_h=-1};recovered=valid;
            reset_sync();clutch=(ClutchState){0};clutch_gear=-1;
        }
        if((hybrid||controller_mode)&&valid&&readable(drive,0x64)&&readable(car,0x43c)&&
           !*(unsigned char *)((char *)self+0x2b4)&&!*(int *)((char *)self+0x2b8)&&
           *(float *)((char *)self+0x2bc)<=0) {
            /* A manual car's brake never becomes reverse throttle. Direction comes from the shifter. */
            *(float *)((char *)self+0x20c)=gas;
            *(float *)((char *)self+0x214)=brake;
            *(float *)((char *)car+0x438)=gas;
            void *spec=*(void **)((char *)drive+0x18);int maximum=2;
            if(readable(spec,0x40))for(int i=2;i<8;i++)
                if(*(float *)((char *)spec+0x20+4*i)>0)maximum=i;
            if(recovered){shifting.up_was=held(&raw,paddle_up);shifting.down_was=held(&raw,paddle_down);}
            int h=h_slot(&raw);
            int actual=*(int *)((char *)drive+0x3c);
            int request;
            if(controller_mode) {
                h=controller_routed_request(controller_gear,*(int *)((char *)self+0x1e4));
                if(recovered&&!sync_event_pending)sync_target=actual;
                request=controller_request(h,actual,synchronizer_model?sync_target:actual,maximum,foot,*(float *)((char *)car+0x42c),synchronizer_model);
                sync_event_pending=0;
            } else request=shift_request(&shifting,h,paddle_enabled&&held(&raw,paddle_up),paddle_enabled&&held(&raw,paddle_down),
                actual,maximum,value,*(float *)((char *)car+0x42c),dt,&value);
            void *engine=*(void **)((char *)drive+0x20);
            float rpm=readable(engine,0x18)?*(float *)((char *)engine+0x14)*9.549304f:0;
            if(!synchronizer_model&&grinding_shift(grind_enabled,shifting.paddles,h,*(int *)((char *)drive+0x3c),request,maximum,foot,rpm))
                InterlockedExchange(&grind_until,(LONG)(GetTickCount()+150));
            if(synchronizer_model){
                /* Keep the requested lever/paddle target separate from native committed gear. */
                int committed=*(int *)((char *)drive+0x3c);
                if(!controller_mode)request=synchronizer_request(&shifting,h,request,committed,&value);
                double ratios[8]={0};if(readable(spec,0x40))for(int i=0;i<8;i++)ratios[i]=*(float *)((char *)spec+0x20+4*i);
                ClutchTarget choice=clutch_choose_target(request,committed,clutch_gear_mask(ratios));
                if(choice.reason){target_rejected(request,choice);request=choice.target;value=foot;}
                else sync_last_rejected=-999;
                sync_target=request;sync_speed=*(float *)((char *)car+0x42c);
                *(int *)((char *)self+0x1e4)=committed;*(int *)((char *)drive+0x38)=committed;
                *(int *)((char *)drive+0x48)=0;
            } else {
                *(int *)((char *)self+0x1e4)=request;
                *(int *)((char *)drive+0x38)=request;
                *(int *)((char *)drive+0x48)=request!=*(int *)((char *)drive+0x3c);
            }
        }
        if(readable(drive,0x64)) {
            *(float *)((char *)self+0x2f4)=foot;
            if(!valid) {
                int actual=*(int *)((char *)drive+0x3c);
                *(int *)((char *)drive+0x38)=actual;*(int *)((char *)drive+0x48)=0;
                *(int *)((char *)self+0x1e4)=actual;
                *(float *)((char *)self+0x20c)=0;
                if(readable(car,0x43c))*(float *)((char *)car+0x438)=0;
            }
            clutch_release=valid?value:0;
            *(int *)((char *)drive+0x60)=3;
            *(int *)((char *)drive+0x44)=1;
            *(float *)((char *)drive+0x14)=clutch_release;
        }
        if(foot<minimum)minimum=foot;
        if(foot>maximum)maximum=foot;
        samples++;
        if(GetTickCount()-last_log>2000) {
            char text[620];
            if(readable(drive,0x64)) {
                void *e=*(void **)((char *)drive+0x20);
                float rpm=readable(e,0x18)?*(float *)((char *)e+0x14)*9.549304f:-1;
                snprintf(text,sizeof(text),"samples=%u engagement=%.4f observed_min=%.4f observed_max=%.4f native_coupling=%.4f native_status=%d native_mode=%d limited_samples=%u last_slip_rad_s=%.6f last_slip_transmitted=%.6f engine_peak=%.6f rpm=%.1f stalled=%d starter=%d isolated=%d gear_request=%d actual_gear=%d manual_mode=%d solver_samples=%u",samples,value,minimum,maximum,*(float *)((char *)drive+0x14),*(int *)((char *)drive+0x60),*(int *)((char *)drive+0x44),limited_samples,last_demand,last_torque,last_peak,rpm,motor.stalled,starter,isolated,*(int *)((char *)self+0x1e4),*(int *)((char *)drive+0x3c),*mode,solver_samples);
                log_status(text);
            }
            if(synchronizer_model){
                snprintf(text,sizeof(text),"sync_steps_captured=%u sync_steps_consumed=%u pending_dt=%.6f target=%d failures=%u",
                    sync_clock.captures,sync_clock.consumes,sync_clock.pending,sync_target,sync_failures);
                log_status(text);
            }
            last_log=GetTickCount();
        }
        player_drive=drive;
        void *engine=readable(player_drive,0x64)?*(void **)((char *)player_drive+0x20):NULL;
        if(engine!=player_engine) {
            memset(&motor,0,sizeof(motor));player_engine=engine;
            void *ctx=readable(car,0x18)?*(void **)((char *)car+0x14):NULL;
            int type=readable(ctx,0x90c)?*(int *)((char *)ctx+0x908):-1;
            char *table=*(char **)0x8a1ccc;
            char *name=type>=0&&type<256?table+type*0x890:NULL;
            strcpy(car_section,"Car.DEFAULT");
            if(readable(name,16)&&memchr(name,0,16)) {
                int valid=1;
                for(const char *p=name;*p;p++)if(!((*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'))valid=0;
                if(valid&&*name)snprintf(car_section,sizeof(car_section),"Car.%s",name);
            }
            last_config=0;log_status(car_section);
        }
        isolated=clutch_down||(readable(player_drive,0x40)&&*(int *)((char *)player_drive+0x3c)==1);
    } else {
        if(player) {
            reset_sync();clutch=(ClutchState){0};clutch_gear=-1;clutch_release=0;
            player_drive=NULL;player_engine=NULL;memset(&motor,0,sizeof(motor));
            shifting=(ShiftState){.last_h=-1};
        }
    }
}
static BOOL CALLBACK own_window(HWND hwnd,LPARAM out)
{DWORD pid;GetWindowThreadProcessId(hwnd,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(hwnd)){*(HWND *)out=hwnd;return FALSE;}return TRUE;}
static void resume_threads(HANDLE *threads,unsigned count)
{
    while(count){HANDLE t=threads[--count];ResumeThread(t);CloseHandle(t);}
}
static int suspend_threads(HANDLE *threads,unsigned *count)
{
    *count=0;
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot==INVALID_HANDLE_VALUE)return 0;
    THREADENTRY32 e={0};e.dwSize=sizeof(e);unsigned found=0;
    int ok=Thread32First(snapshot,&e)!=0;
    if(ok)do {
        if(e.th32OwnerProcessID!=GetCurrentProcessId()||e.th32ThreadID==GetCurrentThreadId())continue;
        if(found==256){ok=0;break;}
        HANDLE t=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT,FALSE,e.th32ThreadID);
        if(!t){ok=0;break;}
        threads[found++]=t;
    } while(Thread32Next(snapshot,&e));
    CloseHandle(snapshot);
    if(ok)for(;*count<found;(*count)++) {
        if(SuspendThread(threads[*count])==(DWORD)-1){ok=0;break;}
    }
    if(ok)return 1;
    for(unsigned i=*count;i<found;i++)CloseHandle(threads[i]);
    resume_threads(threads,*count);*count=0;return 0;
}
static DWORD WINAPI install(void *unused)
{
    (void)unused;
    if(!load_bindings()){log_status("invalid_controls_config=1 no_hooks_installed=1");return 0;}
    char mapping[160],guid_text[64]={0},axis[16]={0},system[MAX_PATH];
    GetPrivateProfileStringA("map","clutch","",mapping,sizeof(mapping),ini_path);
    int wheel_binding=sscanf(mapping,"%63[^|]|%15[^|]|%ld|%ld",guid_text,axis,&released,&pressed)==4&&released!=pressed;
    axis_index=clutch_axis(axis);
    WCHAR wide[64];GUID guid={0};
    MultiByteToWideChar(CP_ACP,0,guid_text,-1,wide,64);
    wheel_binding=wheel_binding&&axis_index>=0&&released>=0&&released<=65535&&pressed>=0&&pressed<=65535&&SUCCEEDED(CLSIDFromString(wide,&guid));
    if(source_kind==SOURCE_WHEEL&&!wheel_binding){log_status("invalid_wheel_binding=1 see_ffb_ini=1");return 0;}
    char shifter_device[64],shifter_mode[16],seq_device[64];
    GetPrivateProfileStringA("shifter","device","",shifter_device,sizeof(shifter_device),ini_path);
    GetPrivateProfileStringA("shifter","mode","",shifter_mode,sizeof(shifter_mode),ini_path);
    hybrid=GetPrivateProfileIntA("shifter","enabled",0,ini_path)&&!strcmp(shifter_mode,"h")&&!_stricmp(shifter_device,guid_text);
    for(int i=0;i<9;i++)h_buttons[i]=-1;
    if(hybrid) {
        h_buttons[0]=GetPrivateProfileIntA("shifter","reverse",0,ini_path)-1;
        for(int i=2;i<9;i++) {char key[16];snprintf(key,sizeof(key),"gear%d",i-1);h_buttons[i]=GetPrivateProfileIntA("shifter",key,0,ini_path)-1;}
        GetPrivateProfileStringA("shifter","seq_device","",seq_device,sizeof(seq_device),ini_path);
        if(!_stricmp(seq_device,guid_text)) {
            paddle_up=GetPrivateProfileIntA("shifter","seq_up",0,ini_path)-1;
            paddle_down=GetPrivateProfileIntA("shifter","seq_down",0,ini_path)-1;
            if(paddle_up<0||paddle_up>=128||paddle_down<0||paddle_down>=128||paddle_up==paddle_down)paddle_up=paddle_down=-1;
        }
        if(h_buttons[0]<0||h_buttons[0]>=128||h_buttons[2]<0||h_buttons[2]>=128||h_buttons[3]<0||h_buttons[3]>=128)hybrid=0;
    }
    DWORD module_wait=GetTickCount();unsigned module_attempts=0;
    do {
        wheel=steering_find_module();
        if(wheel)break;
        if(!steering_guard_retry(1,module_wait,GetTickCount(),++module_attempts)){
            char message[384];snprintf(message,sizeof(message),"wheelfix_module_missing expected=%s attempts=%u no_hooks_installed=1",steering_expected_path,module_attempts);log_status(message);return 0;
        }
        Sleep(50);
    }while(!wheel);
    if(!readable(wheel+0x61300,8)) {log_status("wheel_module_missing=1");return 0;}
    UINT n=GetSystemDirectoryA(system,sizeof(system));
    if(!n || n+13>=sizeof(system))return 0;
    strcat(system,"\\dinput8.dll");
    HMODULE library=LoadLibraryA(system);
    CreateFn create=library?(CreateFn)(void *)GetProcAddress(library,"DirectInput8Create"):NULL;
    if(!create) {log_status("directinput_library_failed=1");return 0;}
    HRESULT hr=create(GetModuleHandleA(NULL),0x800,&IID_IDirectInput8A,(void **)&input,NULL);
    if(FAILED(hr)) {char msg[100];snprintf(msg,sizeof(msg),"directinput_create_failed=0x%08lx",(unsigned long)hr);log_status(msg);return 0;}
    HMODULE xinput=LoadLibraryA("xinput1_4.dll");
    if(xinput)pad_state=(PadStateFn)(void *)GetProcAddress(xinput,"XInputGetState");
    if(xinput)pad_vibrate=(PadVibrateFn)(void *)GetProcAddress(xinput,"XInputSetState");
    if((source_kind==SOURCE_AUTO||source_kind==SOURCE_WHEEL)&&wheel_binding){
        hr=IDirectInput8_CreateDevice(input,&guid,&device,NULL);
        if(FAILED(hr))device=NULL;
        if(source_kind==SOURCE_WHEEL&&!device){log_status("clutch_device_missing=1 no_hooks_installed=1");return 0;}
    }
    if(source_kind==SOURCE_AUTO){
        float release;
        source_kind=device?SOURCE_WHEEL:controller_sample(&release,NULL)?SOURCE_GAMEPAD:SOURCE_KEYBOARD;
    }
    controller_mode=source_kind!=SOURCE_WHEEL||!hybrid;
    char binding_status[160];snprintf(binding_status,sizeof(binding_status),"input_source=%d keyboard_clutch=%d keyboard_ignition=%d gamepad_slot=%lu pad_clutch=0x%x pad_ignition=0x%x wheel_ignition=%d",source_kind,clutch_key,ignition_key,(unsigned long)gamepad_index,pad_clutch,pad_ignition,wheel_ignition+1);log_status(binding_status);
    if(device) {
        raw_state=device->lpVtbl->GetDeviceState;
        HMODULE owner=NULL;char owner_path[MAX_PATH]={0};
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)(uintptr_t)raw_state,&owner);
        GetModuleFileNameA(owner,owner_path,sizeof(owner_path));
        log_status(owner_path);
        if(owner==(HMODULE)wheel) {log_status("raw_input_guard_failed=1");return 0;}
    }
    HWND window=NULL;
    for(int i=0;i<600&&!window;i++){EnumWindows(own_window,(LPARAM)&window);if(!window)Sleep(100);}
    if(!window || (device&&(FAILED(IDirectInputDevice8_SetCooperativeLevel(device,window,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE)) ||
        FAILED(IDirectInputDevice8_SetDataFormat(device,&c_dfDIJoystick2))))) {log_status("device_setup_failed=1");return 0;}
    if(device) {
        DIPROPRANGE range={{sizeof(range),sizeof(DIPROPHEADER),axis_offsets[axis_index],DIPH_BYOFFSET},0,65535};
        if(FAILED(IDirectInputDevice8_SetProperty(device,DIPROP_RANGE,&range.diph))) {log_status("axis_range_failed=1");return 0;}
        IDirectInputDevice8_Acquire(device);
    }
    load_button_config(1);
    rumble_configure();
    dualsense_configure();
    log_status(pad_vibrate?"xbox_rumble_available=1":"xbox_rumble_available=0 reason=XInputSetState_missing");
    unsigned char *base=(unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt=(IMAGE_NT_HEADERS *)(base+((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *section=IMAGE_FIRST_SECTION(nt);
    void **slot=NULL;int matches=0;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        if(memcmp(section[i].Name,".rdata",6))continue;
        void **start=(void **)(base+section[i].VirtualAddress);
        for(unsigned j=0;j<section[i].Misc.VirtualSize/sizeof(void *);j++)
            if(start[j]==(void *)0x41e2f0){slot=start+j;matches++;}
    }
    if(matches!=1) {log_status("input_vtable_guard_failed=1");return 0;}
    const unsigned char shifter_bytes[]={0x55,0x57,0x56,0x53,0x81,0xec,0xec,0x00,0x00,0x00};
    /* Require the exact dependency before using its captured input object. */
    DWORD guard_wait=GetTickCount();unsigned guard_attempts=0;
    while(!steering_identity()){
        if(!steering_guard_retry(steering_retryable,guard_wait,GetTickCount(),++guard_attempts)){
            log_status("shifter_owner_guard_failed=1 no_hooks_installed=1");return 0;
        }
        Sleep(50);
    }
    if(guard_attempts){char message[96];snprintf(message,sizeof(message),"wheelfix_guard_recovered=1 retries=%u elapsed_ms=%lu",guard_attempts,(unsigned long)(GetTickCount()-guard_wait));log_status(message);}
    if(!shifter_storage_guard()){log_status("shifter_storage_guard_failed=1 no_hooks_installed=1");return 0;}
    const unsigned char torque_bytes[]={0x83,0xec,0x08,0x56,0x8b,0xf1};
    const unsigned char post_bytes[]={0x83,0xec,0x08,0x56,0x8b,0xf1,0x8b,0x56,0x18};
    const unsigned char initial_bytes[]={0x51,0x56,0x8b,0xf1,0x8b,0x46,0x44};
    const unsigned char finish_bytes[]={0x83,0xec,0x08,0x56,0x8b,0xf1};
    const unsigned char distribute_bytes[]={0x51,0x83,0x79,0x3c,0x01};
    const unsigned char wheel_value_bytes[]={0x8b,0x51,0x18};
    const unsigned char shift_bytes[6]={0x8b,0x54,0x24,0x0c,0x84,0xd2};
    const unsigned char rest_bytes[6]={0x51,0x57,0x8b,0x7c,0x24,0x0c};
    const uint32_t pause_record[8]={0xff,0,0xbd,0x571790,0x000c0001,0,0,0x7a14f4};
    const unsigned char pause_bytes[10]={0x8b,0x44,0x24,0x0c,0x8b,0x4c,0x24,0x08,0x56,0x8b};
    if(controller_mode&&(memcmp((void *)0x7fe150,pause_record,32)||memcmp((void *)0x571790,pause_bytes,10))){
        log_status("pause_scanner_guard_failed=1 no_hooks_installed=1");return 0;
    }
    int override_throttle=source_kind==SOURCE_GAMEPAD&&pad_throttle;
    const unsigned char throttle_bytes[10]={0x8b,0x4c,0x24,0x14,0x55,0x56,0x8b,0x74,0x24,0x18};
    const uint32_t throttle_records[2][8]={
        {0xff,0,0x92,0x579150,0x00400001,0x300,0,0x7a2d44},
        {0xff,0,0x93,0x579150,0x00580001,0x301,0,0x7a2d44}};
    if(override_throttle&&(memcmp((void *)0x579150,throttle_bytes,10)||
       memcmp((void *)0x7fe190,throttle_records,sizeof(throttle_records)))){
        log_status("throttle_scanner_guard_failed=1 no_hooks_installed=1");return 0;
    }
    void **post_slot=(void **)0x7a2380;
    struct Hook {unsigned char *entry;const unsigned char *bytes;size_t n;void *target;unsigned char *trampoline;DWORD protection;} hooks[]={
        {(void *)0x593ee0,torque_bytes,sizeof(torque_bytes),(void *)engine_torque,NULL,0},
        {(void *)0x594890,initial_bytes,sizeof(initial_bytes),(void *)initial_torque,NULL,0},
        {(void *)0x5aa460,finish_bytes,sizeof(finish_bytes),(void *)finish_coupling,NULL,0},
        {wheel+0xd1e0,shifter_bytes,sizeof(shifter_bytes),(void *)shifter_update,NULL,0},
        {(void *)0x598e90,rest_bytes,sizeof(rest_bytes),(void *)classify_rest,NULL,0},
        {(void *)0x407fe0,shift_bytes,sizeof(shift_bytes),(void *)shift_up,NULL,0},
        {(void *)0x4080d0,shift_bytes,sizeof(shift_bytes),(void *)shift_down,NULL,0}
    };
    unsigned hook_count=controller_mode?7:5;
    if(memcmp((void *)0x5aa390,post_bytes,sizeof(post_bytes))||*post_slot!=(void *)0x5aa390||
       memcmp((void *)0x594610,distribute_bytes,sizeof(distribute_bytes))||
       memcmp((void *)0x5941d0,wheel_value_bytes,sizeof(wheel_value_bytes))) {
        log_status("engine_build_guard_failed=1");return 0;
    }
    for(unsigned i=0;i<hook_count;i++)if(memcmp(hooks[i].entry,hooks[i].bytes,hooks[i].n)) {
        log_status("spring_clutch_build_guard_failed=1");return 0;
    }
    unsigned prepared=0,suspended=0;HANDLE threads[256];
    DWORD old_input=0,old_post_page=0,old_pause_page=0,old_throttle_page=0,ignored;
    for(unsigned i=0;i<hook_count;i++) {
        struct Hook *h=&hooks[i];
        h->trampoline=VirtualAlloc(NULL,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
        if(!h->trampoline)goto rollback;
        memcpy(h->trampoline,h->bytes,h->n);h->trampoline[h->n]=0xe9;
        *(int32_t *)(h->trampoline+h->n+1)=(int32_t)((uintptr_t)(h->entry+h->n)-(uintptr_t)(h->trampoline+h->n+5));
        FlushInstructionCache(GetCurrentProcess(),h->trampoline,h->n+5);
        if(!VirtualProtect(h->entry,h->n,PAGE_EXECUTE_READWRITE,&h->protection))goto rollback;
        prepared++;
    }
    if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&old_input))goto rollback;
    if(!VirtualProtect(post_slot,sizeof(*post_slot),PAGE_READWRITE,&old_post_page))goto rollback;
    if(controller_mode&&!VirtualProtect((void *)0x7fe15c,4,PAGE_READWRITE,&old_pause_page))goto rollback;
    if(override_throttle&&!VirtualProtect((void *)0x7fe190,64,PAGE_READWRITE,&old_throttle_page))goto rollback;
    old_torque=(TorqueFn)hooks[0].trampoline;old_initial=(PostFn)hooks[1].trampoline;
    old_shifter_update=(ShifterUpdateFn)hooks[3].trampoline;
    old_rest=(RestFn)hooks[4].trampoline;
    if(controller_mode){native_shift_up=(ShiftEventFn)hooks[5].trampoline;native_shift_down=(ShiftEventFn)hooks[6].trampoline;}
    InitializeCriticalSection(&shifter_lock);
    old_finish=(PostFn)hooks[2].trampoline;old_post=(PostFn)*post_slot;
    old_distribute=(UpdateFn)0x594610;old_update=(UpdateFn)*slot;
    realistic=GetPrivateProfileIntA("Realism","Enabled",1,realism_path)!=0;
    synchronizer_model=GetPrivateProfileIntA("Clutch","SynchronizerModel",0,realism_path)!=0;
    load_clutch_estimates();
    /* A delayed wheel connection must not expose half-written jumps to the game thread. */
    if(!suspend_threads(threads,&suspended))goto rollback;
    for(unsigned t=0;t<suspended;t++) {
        CONTEXT context={0};context.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(threads[t],&context))goto rollback;
        for(unsigned i=0;i<hook_count;i++)
            if(context.Eip>=(uintptr_t)hooks[i].entry&&context.Eip<(uintptr_t)(hooks[i].entry+hooks[i].n))goto rollback;
    }
    for(unsigned i=0;i<hook_count;i++) {
        struct Hook *h=&hooks[i];
        h->entry[0]=0xe9;*(int32_t *)(h->entry+1)=(int32_t)((uintptr_t)h->target-(uintptr_t)(h->entry+5));
        for(size_t j=5;j<h->n;j++)h->entry[j]=0x90;
        FlushInstructionCache(GetCurrentProcess(),h->entry,h->n);
    }
    *post_slot=(void *)engine_post;*slot=(void *)update;hooks_ready=1;
    if(controller_mode){native_pause=(PauseScanFn)0x571790;InterlockedExchangePointer((void **)0x7fe15c,(void *)pause_scan);}
    if(override_throttle){
        InterlockedExchangePointer((void **)0x7fe19c,(void *)scan_throttle);
        InterlockedExchangePointer((void **)0x7fe1bc,(void *)scan_throttle);
    }
    resume_threads(threads,suspended);suspended=0;
    VirtualProtect(post_slot,sizeof(*post_slot),old_post_page,&ignored);
    VirtualProtect(slot,sizeof(*slot),old_input,&ignored);
    if(old_throttle_page)VirtualProtect((void *)0x7fe190,64,old_throttle_page,&ignored);
    if(old_pause_page)VirtualProtect((void *)0x7fe15c,4,old_pause_page,&ignored);
    for(unsigned i=hook_count;i>0;i--)VirtualProtect(hooks[i-1].entry,hooks[i-1].n,hooks[i-1].protection,&ignored);
    log_status("shifter_owner_guarded=1 capture_masking_preserved=1 handback=neutral_rearm");
    log_status("pre_race_hold_handoff=1 native_forced_neutral_preserved=1");
    log_status("neutral_coast_guarded=1 human_manual_neutral_only=1 native_braking_preserved=1");
    log_status("ignition=remappable clutch_required=95pct native_pause_preserved=1");
    if(override_throttle){char message[120];snprintf(message,sizeof(message),"throttle_source=configured_gamepad slot=%lu binding=0x%x alternate_zero=1",(unsigned long)gamepad_index,pad_throttle);log_status(message);}
    if(controller_mode)log_status("controller_shift_order=R,N,1,2,3,4,5,6 native_bindings=preserved reverse_requires_clutch_and_stop=1");
    log_status(synchronizer_model?
        "analog_clutch_hook_installed=1 model=synchronizer native_lock_bypassed=1 prediction=next_step_approx toggle=WheelFix_fallback bindings_preserved=1":
        "analog_clutch_hook_installed=1 model=implicit_spring_damper native_lock_bypassed=1 toggle=WheelFix_fallback bindings_preserved=1");
    if(handbrake_enabled)log_status("handbrake=tap_toggle_hold_momentary hold_ms=350 native_binding=preserved realism_independent=1");
    goto pump;
rollback:
    resume_threads(threads,suspended);
    if(old_throttle_page)VirtualProtect((void *)0x7fe190,64,old_throttle_page,&ignored);
    if(old_post_page)VirtualProtect(post_slot,sizeof(*post_slot),old_post_page,&ignored);
    if(old_input)VirtualProtect(slot,sizeof(*slot),old_input,&ignored);
    if(old_pause_page)VirtualProtect((void *)0x7fe15c,4,old_pause_page,&ignored);
    while(prepared){prepared--;VirtualProtect(hooks[prepared].entry,hooks[prepared].n,hooks[prepared].protection,&ignored);}
    for(unsigned i=0;i<hook_count;i++)if(hooks[i].trampoline)VirtualFree(hooks[i].trampoline,0,MEM_RELEASE);
    log_status("hook_install_aborted_no_patches_written=1");return 0;
pump:
    /* DirectInput's raw-input window belongs to its creating thread. Keep dispatching its messages. */
    if(!SetTimer(NULL,0,16,release_button_keys)) {
        char message[96];
        snprintf(message,sizeof(message),"wheel_button_timer_failed=%lu button_actions_unavailable=1 F7_available=1",(unsigned long)GetLastError());
        log_status(message);
    } else log_status("wheel_button_timer_started=1 interval_ms=16");
    MSG message;
    while(GetMessageA(&message,NULL,0,0)>0) {
        TranslateMessage(&message);DispatchMessageA(&message);
    }
    rumble_stop();
    dualsense_shutdown();
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID unused)
{
    if(reason==DLL_PROCESS_DETACH){rumble_stop();dualsense_shutdown();return TRUE;}
    (void)unused;if(reason!=DLL_PROCESS_ATTACH)return TRUE;
    DisableThreadLibraryCalls(module);
    InitializeCriticalSection(&button_lock);
    InitializeCriticalSection(&rumble_lock);
    InitializeCriticalSection(&dualsense_lock);
    char path[MAX_PATH];DWORD n=GetModuleFileNameA(NULL,path,sizeof(path));
    char *slash=n&&n<sizeof(path)?strrchr(path,'\\'):NULL;
    if(!slash || slash-path+50>=MAX_PATH)return TRUE;
    strcpy(slash,"\\scripts\\NFSU2.GearGrind.wav");strcpy(grind_path,path);
    strcpy(slash,"\\ffb.ini");strcpy(ini_path,path);
    strcpy(slash,"\\scripts\\NFSU2.Realism.ini");strcpy(realism_path,path);
    strcpy(slash,"\\scripts\\NFSU2.Realism.log");strcpy(log_path,path);
    FILE *f=fopen(log_path,"w");if(f)fclose(f);
    unsigned char *base=(unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt=(IMAGE_NT_HEADERS *)(base+((IMAGE_DOS_HEADER *)base)->e_lfanew);
    const unsigned char expected[]={0x51,0x56,0x8b,0xf1,0x83,0xbe,0xac,0x02,0,0,0xff};
    if((uintptr_t)base!=0x400000 || nt->OptionalHeader.AddressOfEntryPoint!=0x35b8d1 ||
        memcmp((void *)0x41e2f0,expected,sizeof(expected))) {log_status("build_guard_failed=1");return TRUE;}
    HANDLE t=CreateThread(NULL,0,install,NULL,0,NULL);if(t)CloseHandle(t);
    return TRUE;
}
#endif
