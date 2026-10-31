/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Fixed-buffer module entry around the production flight director core. */
#include "iaf_module.h"
#include <string.h>

#if defined(__wasm__)
#define IAF_EXPORT(name) __attribute__((export_name(name)))
#else
#define IAF_EXPORT(name)
#endif

typedef struct slot { uint32_t generation, live; iaf_module_state state; } slot;
static slot g_slots[IAF_MODULE_CAPACITY];
static iaf_module_request g_request;
static iaf_module_response g_response;
static uint32_t g_busy;

static slot *find(uint32_t h) {
    uint32_t i = h & 0xffu, generation = h >> 8;
    if (!h || i >= IAF_MODULE_CAPACITY || !generation) return 0;
    slot *s = &g_slots[i];
    return s->live && s->generation == generation ? s : 0;
}

static int valid_phase(int32_t p) { return p >= PH_NONE && p <= PH_H_LIFT; }
static uint32_t next_generation(uint32_t generation) {
    return generation < 0x00ffffffu ? generation + 1u : 0;
}

IAF_EXPORT("iaf_module_query") uint32_t iaf_module_query(uint32_t key) {
    static const uint32_t v[] = { IAF_MODULE_ABI, IAF_MODULE_CAPACITY, sizeof(iaf_module_request), sizeof(iaf_module_response), sizeof(iaf_module_state),
        offsetof(iaf_module_request,state), offsetof(iaf_module_request,landing), offsetof(iaf_module_request,a), offsetof(iaf_module_request,b),
        offsetof(iaf_module_request,args), offsetof(iaf_module_request,now), offsetof(iaf_module_request,flag), offsetof(iaf_module_response,state),
        offsetof(iaf_module_response,command), offsetof(iaf_module_response,landing), offsetof(iaf_module_response,runway), offsetof(iaf_module_response,values) };
    return key < sizeof(v)/sizeof(v[0]) ? v[key] : UINT32_MAX;
}
IAF_EXPORT("iaf_module_request_offset") uintptr_t iaf_module_request_offset(void) { return (uintptr_t)&g_request; }
IAF_EXPORT("iaf_module_response_offset") uintptr_t iaf_module_response_offset(void) { return (uintptr_t)&g_response; }
IAF_EXPORT("iaf_module_create") uint32_t iaf_module_create(int32_t phase) {
    if (g_busy || !valid_phase(phase)) return 0;
    for (uint32_t i=0; i<IAF_MODULE_CAPACITY; ++i) {
        slot *s=&g_slots[i];
        if (s->live || s->generation == 0x00ffffffu) continue;
        s->generation=next_generation(s->generation); s->live=1;
        iaf_fd_kin_reset(&s->state.kin); iaf_landing_reset(&s->state.landing,phase);
        return s->generation << 8 | i;
    }
    return 0;
}
IAF_EXPORT("iaf_module_drop") uint32_t iaf_module_drop(uint32_t h) {
    slot *s=g_busy ? 0 : find(h);
    if (!s) return IAF_M_BAD_HANDLE;
    memset(&s->state,0,sizeof(s->state)); s->live=0; return IAF_M_OK;
}
IAF_EXPORT("iaf_module_reset") uint32_t iaf_module_reset(void) {
    if (g_busy) return IAF_M_BUSY;
    for (uint32_t i=0;i<IAF_MODULE_CAPACITY;++i) {
        g_slots[i].live=0; /* generations persist; exhausted slots are retired, never wrapped */
        memset(&g_slots[i].state,0,sizeof(g_slots[i].state));
    }
    memset(&g_request,0,sizeof(g_request)); memset(&g_response,0,sizeof(g_response)); return IAF_M_OK;
}
static uint32_t done(uint32_t status) {
    g_response.abi=IAF_MODULE_ABI; g_response.size=sizeof(g_response); g_response.status=status; g_busy=0; return status;
}
IAF_EXPORT("iaf_module_dispatch") uint32_t iaf_module_dispatch(uint32_t request_size) {
    if (g_busy) return IAF_M_BUSY;
    g_busy=1; iaf_module_request q=g_request; memset(&g_response,0,sizeof(g_response));
    if (request_size!=sizeof(q) || q.abi!=IAF_MODULE_ABI || q.size!=sizeof(q)) return done(IAF_M_BAD_SIZE);
    if (q.op<IAF_M_GET || q.op>IAF_M_WRAP) return done(IAF_M_BAD_OP);
    iaf_module_response *r=&g_response; slot *s=0;
    if (q.op<=IAF_M_ROLLOUT_BEGIN && q.op!=IAF_M_TICK_DT && q.op!=IAF_M_RUNWAY && q.op!=IAF_M_FRAME && q.op!=IAF_M_POINT && q.op!=IAF_M_YAW) {
        s=find(q.handle); if (!s) return done(IAF_M_BAD_HANDLE);
    }
    switch(q.op) {
    case IAF_M_GET: break;
    case IAF_M_SET_STATE:
        /* Three retries abort the flight. A live slot may contain 0..2 retries;
         * 3 is observable only in the staged terminal ABORT response, never committed.
         * Reject before assignment so invalid counters cannot reach the core's ++. */
        if (!valid_phase(q.state.landing.phase) || q.state.landing.go_arounds < 0 ||
            q.state.landing.go_arounds > 2) return done(IAF_M_BAD_STATE);
        s->state=q.state; break;
    case IAF_M_SEED: r->result=iaf_fd_kin_seed(&s->state.kin,q.a,q.flag); break;
    case IAF_M_LINE_UP: iaf_fd_kin_line_up(&s->state.kin,q.args[0]); break;
    case IAF_M_TICK_DT:
        /* Malformed integer counters must not invoke signed-overflow UB. Floating-point
         * semantics (including freq=0) otherwise remain the core's existing behaviour. */
        if (q.prev && ((q.prev > 0 && q.now < INT64_MIN + q.prev) ||
                       (q.prev < 0 && q.now > INT64_MAX + q.prev))) return done(IAF_M_BAD_STATE);
        r->result=iaf_fd_tick_dt(q.now,q.prev,q.freq,&r->values[0]); break;
    case IAF_M_RUNWAY: r->result=iaf_runway_from_nodes(q.a,q.b,q.flag,&r->runway); break;
    case IAF_M_FRAME: iaf_runway_frame(&q.landing.runway,q.args[0],q.args[1],&r->values[0],&r->values[1]); break;
    case IAF_M_POINT: iaf_runway_point(&q.landing.runway,q.args[0],q.args[1],r->values); break;
    case IAF_M_YAW: r->values[0]=iaf_runway_yaw(&q.landing.runway); break;
    case IAF_M_STEER: r->values[0]=iaf_fd_steer(&s->state.kin,q.a,q.b,q.args[0]); break;
    case IAF_M_FLY: iaf_fd_plane_fly(&s->state.kin,q.args[0],q.args[1],q.args[2],q.args[3],q.args[4],q.args[5],q.args[6],q.args[7],&r->command); break;
    case IAF_M_ROLLOUT: r->result=iaf_fd_plane_rollout(&s->state.kin,&q.landing.runway,q.args[0],q.args[1],q.args[2],q.args[3],&r->command); break;
    case IAF_M_TAKEOFF_ROLL: r->result=iaf_fd_plane_takeoff_roll(&s->state.kin,&q.landing.runway,q.args[0],q.args[1],q.args[2],&r->command); break;
    case IAF_M_LANDING: {
        iaf_module_state next=s->state; iaf_landing_step(&next.landing,&next.kin,&q.landing,&r->landing);
        r->state=next; return done(IAF_M_OK);
    }
    case IAF_M_TOUCHDOWN: {
        iaf_module_state next=s->state; r->result=iaf_landing_touchdown(&next.landing,q.args[0],q.args[1],q.flag,q.args[2]);
        r->state=next; return done(IAF_M_OK);
    }
    case IAF_M_ROLLOUT_BEGIN: iaf_landing_rollout_begin(&s->state.kin,q.a); break;
    case IAF_M_CLAMP: r->values[0]=iaf_fd_clamp(q.args[0],q.args[1],q.args[2]); break;
    case IAF_M_WRAP: r->values[0]=iaf_fd_wrap(q.args[0]); break;
    default: return done(IAF_M_BAD_OP);
    }
    if (s) r->state=s->state;
    return done(IAF_M_OK);
}