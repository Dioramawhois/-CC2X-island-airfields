/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IAF_MODULE_H
#define IAF_MODULE_H
#include <stddef.h>
#include <stdint.h>
#include "iaf_fd_core.h"

#define IAF_MODULE_ABI 1u
#define IAF_MODULE_CAPACITY 32u
/* Little-endian wasm32 wire records. No native pointers. One synchronous caller per instance.
 * Write the ENTIRE request, call dispatch, then copy the ENTIRE response before the next call.
 * State remains in module slots. SET_STATE supports the still-native helicopter/takeoff adapter
 * and staged host effects; it does not execute a second copy of any algorithm.
 * SET_STATE requires phase PH_NONE..PH_H_LIFT and go_arounds 0..2. Retry count 3
 * exists only in a staged terminal ABORT response; host must drop, not commit it.
 * Rejected requests do not change slot state.
 * No floating-point input is sanitised: NaN/signed-zero semantics belong to the core. */
enum iaf_module_op {
    IAF_M_GET = 1, IAF_M_SET_STATE, IAF_M_SEED, IAF_M_LINE_UP, IAF_M_TICK_DT,
    IAF_M_RUNWAY, IAF_M_FRAME, IAF_M_POINT, IAF_M_YAW, IAF_M_STEER,
    IAF_M_FLY, IAF_M_ROLLOUT, IAF_M_TAKEOFF_ROLL, IAF_M_LANDING,
    IAF_M_TOUCHDOWN, IAF_M_ROLLOUT_BEGIN, IAF_M_CLAMP, IAF_M_WRAP
};
enum { IAF_M_OK, IAF_M_BAD_HANDLE, IAF_M_BAD_OP, IAF_M_BAD_SIZE, IAF_M_BUSY, IAF_M_BAD_STATE };
typedef struct iaf_module_state { iaf_fd_kin kin; iaf_landing_state landing; } iaf_module_state;
typedef struct iaf_module_request {
    uint32_t abi, size, op, handle;
    iaf_module_state state;
    iaf_landing_input landing;
    double a[3], b[3], args[8];
    int64_t now, prev, freq;
    int32_t flag;
    uint32_t reserved;
} iaf_module_request;
typedef struct iaf_module_response {
    uint32_t abi, size, status;
    int32_t result;
    iaf_module_state state;
    iaf_fd_cmd command;
    iaf_landing_output landing;
    iaf_runway runway;
    double values[3];
} iaf_module_response;

/* query: 0 ABI, 1 capacity, 2 request size, 3 response size, 4 state size,
 * 5 request.state, 6 request.landing, 7 request.a, 8 request.b, 9 request.args,
 * 10 request.now, 11 request.flag, 12 response.state, 13 response.command,
 * 14 response.landing, 15 response.runway, 16 response.values; other => UINT32_MAX. */
uint32_t iaf_module_query(uint32_t key);
/* Native addresses only for native unit tests; these exports return i32 linear-memory offsets
 * in the actual wasm32 module. Host MUST range-check offset+queried size without overflow. */
uintptr_t iaf_module_request_offset(void);
uintptr_t iaf_module_response_offset(void);
uint32_t iaf_module_create(int32_t phase); /* opaque generation/index handle; 0 if full/invalid */
uint32_t iaf_module_drop(uint32_t handle);
uint32_t iaf_module_reset(void); /* invalidates all handles, clears buffers; no generation reuse */
uint32_t iaf_module_dispatch(uint32_t request_size); /* result also in response.status */

#define IAF_ASSERT_SIZE(t,n) _Static_assert(sizeof(t)==(n), #t " wire size")
#define IAF_ASSERT_OFF(t,f,n) _Static_assert(offsetof(t,f)==(n), #t "." #f " wire offset")
IAF_ASSERT_SIZE(iaf_fd_kin,48); IAF_ASSERT_SIZE(iaf_runway,48); IAF_ASSERT_SIZE(iaf_fd_cmd,48);
IAF_ASSERT_SIZE(iaf_landing_state,32); IAF_ASSERT_OFF(iaf_landing_state,max_lat,16);
IAF_ASSERT_SIZE(iaf_landing_input,104); IAF_ASSERT_OFF(iaf_landing_input,runway_granted,96);
IAF_ASSERT_SIZE(iaf_landing_output,104); IAF_ASSERT_OFF(iaf_landing_output,action,88);
IAF_ASSERT_SIZE(iaf_module_state,80); IAF_ASSERT_OFF(iaf_module_state,landing,48);
IAF_ASSERT_SIZE(iaf_module_request,344);
IAF_ASSERT_OFF(iaf_module_request,state,16); IAF_ASSERT_OFF(iaf_module_request,landing,96);
IAF_ASSERT_OFF(iaf_module_request,a,200); IAF_ASSERT_OFF(iaf_module_request,b,224);
IAF_ASSERT_OFF(iaf_module_request,args,248); IAF_ASSERT_OFF(iaf_module_request,now,312);
IAF_ASSERT_OFF(iaf_module_request,flag,336);
IAF_ASSERT_SIZE(iaf_module_response,320); IAF_ASSERT_OFF(iaf_module_response,state,16);
IAF_ASSERT_OFF(iaf_module_response,command,96); IAF_ASSERT_OFF(iaf_module_response,landing,144);
IAF_ASSERT_OFF(iaf_module_response,runway,248); IAF_ASSERT_OFF(iaf_module_response,values,296);
#endif
