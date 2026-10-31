/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Included only by the native adapter. No domain implementation fallback. */
#include "wasm_host.h"
#include <setjmp.h>
static jmp_buf wasm_abort;
static int wasm_entered;
static DWORD wasm_thread;
static void wasm_fault(void) {
    g_on=0; A->log(PLUGIN,CC2X_LOG_ERROR,"wasm FD disabled: %s",iaf_wasm_error());
    if(wasm_entered) longjmp(wasm_abort,1);
}
static iaf_module_response wasm_call(iaf_module_request *q) {
    iaf_module_response r={0};
    if(!iaf_wasm_dispatch(q,&r)) wasm_fault();
    return r;
}
typedef struct {iaf_fd_kin *kin;iaf_landing_state *land;uint32_t handle;} wasm_binding;
static wasm_binding wasm_slots[IAF_MODULE_CAPACITY];
static wasm_binding *wasm_slot(const iaf_fd_kin *k) {
    for(unsigned i=0;i<IAF_MODULE_CAPACITY;i++)if(wasm_slots[i].kin==k)return &wasm_slots[i];
    wasm_fault();return NULL;
}
static void wasm_new(iaf_fd_kin *k,iaf_landing_state *l,int phase) {
    wasm_binding *s=NULL;
    for(unsigned i=0;i<IAF_MODULE_CAPACITY;i++)if(wasm_slots[i].kin==k){s=&wasm_slots[i];break;}
    if(s) {if(!iaf_wasm_drop(s->handle))wasm_fault();}
    else for(unsigned i=0;i<IAF_MODULE_CAPACITY;i++)if(!wasm_slots[i].kin){s=&wasm_slots[i];break;}
    if(!s){wasm_fault();return;}
    s->kin=k;s->land=l;if(!iaf_wasm_create(phase,&s->handle))wasm_fault();
    iaf_module_request q={0};q.op=IAF_M_GET;q.handle=s->handle;iaf_module_response r=wasm_call(&q);*k=r.state.kin;*l=r.state.landing;
}
static void wasm_end(iaf_fd_kin *k) {wasm_binding *s=wasm_slot(k);if(!iaf_wasm_drop(s->handle))wasm_fault();memset(s,0,sizeof *s);}
static iaf_module_request wasm_req(const iaf_fd_kin *k,unsigned op) {
    iaf_module_request q={0};q.op=op;
    if(k){wasm_binding *s=wasm_slot(k);q.handle=s->handle;q.state.kin=*k;q.state.landing=*s->land;q.op=IAF_M_SET_STATE;wasm_call(&q);q.op=op;}
    return q;
}
static double wb_clamp(double x,double lo,double hi){iaf_module_request q=wasm_req(NULL,IAF_M_CLAMP);q.args[0]=x;q.args[1]=lo;q.args[2]=hi;return wasm_call(&q).values[0];}
static double wb_wrap(double a){iaf_module_request q=wasm_req(NULL,IAF_M_WRAP);q.args[0]=a;return wasm_call(&q).values[0];}
static int32_t wb_dt(int64_t n,int64_t p,int64_t f,double *dt){iaf_module_request q=wasm_req(NULL,IAF_M_TICK_DT);q.now=n;q.prev=p;q.freq=f;iaf_module_response r=wasm_call(&q);*dt=r.values[0];return r.result;}
static int32_t wb_runway(const double a[3],const double b[3],int32_t rev,iaf_runway *out){iaf_module_request q=wasm_req(NULL,IAF_M_RUNWAY);memcpy(q.a,a,24);memcpy(q.b,b,24);q.flag=rev;iaf_module_response r=wasm_call(&q);if(r.result)*out=r.runway;return r.result;}
static void wb_frame(const iaf_runway *rw,double x,double z,double *s,double *lat){iaf_module_request q=wasm_req(NULL,IAF_M_FRAME);q.landing.runway=*rw;q.args[0]=x;q.args[1]=z;iaf_module_response r=wasm_call(&q);*s=r.values[0];*lat=r.values[1];}
static void wb_point(const iaf_runway *rw,double s,double lat,double out[3]){iaf_module_request q=wasm_req(NULL,IAF_M_POINT);q.landing.runway=*rw;q.args[0]=s;q.args[1]=lat;iaf_module_response r=wasm_call(&q);memcpy(out,r.values,24);}
static double wb_yaw(const iaf_runway *rw){iaf_module_request q=wasm_req(NULL,IAF_M_YAW);q.landing.runway=*rw;return wasm_call(&q).values[0];}
static int32_t wb_seed(iaf_fd_kin *k,const double vel[3],int32_t to){iaf_module_request q=wasm_req(k,IAF_M_SEED);memcpy(q.a,vel,24);q.flag=to;iaf_module_response r=wasm_call(&q);*k=r.state.kin;return r.result;}
static void wb_line(iaf_fd_kin *k,double yaw){iaf_module_request q=wasm_req(k,IAF_M_LINE_UP);q.args[0]=yaw;*k=wasm_call(&q).state.kin;}
static double wb_steer(const iaf_fd_kin *k,const double a[3],const double b[3],double rate){iaf_module_request q=wasm_req(k,IAF_M_STEER);memcpy(q.a,a,24);memcpy(q.b,b,24);q.args[0]=rate;return wasm_call(&q).values[0];}
static void wb_fly(iaf_fd_kin *k,double y,double dt,double rate,double speed,double alt,double vz,double sink,double pitch,iaf_fd_cmd *cmd){iaf_module_request q=wasm_req(k,IAF_M_FLY);double args[]={y,dt,rate,speed,alt,vz,sink,pitch};memcpy(q.args,args,sizeof args);iaf_module_response r=wasm_call(&q);*k=r.state.kin;*cmd=r.command;}
static int32_t wb_roll(iaf_fd_kin *k,const iaf_runway *rw,double s,double lat,double dt,double vy,iaf_fd_cmd *cmd){iaf_module_request q=wasm_req(k,IAF_M_ROLLOUT);q.landing.runway=*rw;q.args[0]=s;q.args[1]=lat;q.args[2]=dt;q.args[3]=vy;iaf_module_response r=wasm_call(&q);*k=r.state.kin;*cmd=r.command;return r.result;}
static int32_t wb_takeoff(iaf_fd_kin *k,const iaf_runway *rw,double lat,double dt,double vy,iaf_fd_cmd *cmd){iaf_module_request q=wasm_req(k,IAF_M_TAKEOFF_ROLL);q.landing.runway=*rw;q.args[0]=lat;q.args[1]=dt;q.args[2]=vy;iaf_module_response r=wasm_call(&q);*k=r.state.kin;*cmd=r.command;return r.result;}
static void wb_landing(iaf_landing_state *next,const iaf_fd_kin *k,const iaf_landing_input *in,iaf_landing_output *out){iaf_module_request q=wasm_req(k,IAF_M_LANDING);q.state.landing=*next;/* caller's staged copy initially equals bound state */q.landing=*in;iaf_module_response r=wasm_call(&q);*next=r.state.landing;*out=r.landing;}
static int32_t wb_touch(const iaf_fd_kin *k,iaf_landing_state *l,double s,double h,int32_t hit,double speed){iaf_module_request q=wasm_req(k,IAF_M_TOUCHDOWN);q.args[0]=s;q.args[1]=h;q.args[2]=speed;q.flag=hit;iaf_module_response r=wasm_call(&q);*l=r.state.landing;return r.result;}
static void wb_begin(iaf_fd_kin *k,const double vel[3]){iaf_module_request q=wasm_req(k,IAF_M_ROLLOUT_BEGIN);memcpy(q.a,vel,24);*k=wasm_call(&q).state.kin;}
#define iaf_fd_clamp wb_clamp
#define iaf_fd_wrap wb_wrap
#define iaf_fd_tick_dt wb_dt
#define iaf_runway_from_nodes wb_runway
#define iaf_runway_frame wb_frame
#define iaf_runway_point wb_point
#define iaf_runway_yaw wb_yaw
#define iaf_fd_kin_seed wb_seed
#define iaf_fd_kin_line_up wb_line
#define iaf_fd_steer wb_steer
#define iaf_fd_plane_fly wb_fly
#define iaf_fd_plane_rollout wb_roll
#define iaf_fd_plane_takeoff_roll wb_takeoff
#define iaf_landing_step wb_landing
#define iaf_landing_touchdown wb_touch
#define iaf_landing_rollout_begin wb_begin
