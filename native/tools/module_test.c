/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Native ABI regression for the same wrapper compiled into the production Wasm module. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../plugin/iaf_module.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static iaf_module_request *rq(void) { return (iaf_module_request *)iaf_module_request_offset(); }
static iaf_module_response *rs(void) { return (iaf_module_response *)iaf_module_response_offset(); }
static uint32_t call(uint32_t op, uint32_t h) {
    iaf_module_request *q = rq();
    q->abi = IAF_MODULE_ABI; q->size = sizeof(*q); q->op = op; q->handle = h;
    return iaf_module_dispatch(sizeof(*q));
}

int main(void) {
    CHECK(iaf_module_query(0) == 1 && iaf_module_query(1) == 32 && iaf_module_query(2) == 344);
    CHECK(iaf_module_query(3) == 320 && iaf_module_query(16) == 296 && iaf_module_query(17) == UINT32_MAX);
    CHECK(!iaf_module_create(-1) && !iaf_module_create(PH_H_LIFT + 1));
    uint32_t h = iaf_module_create(PH_INBOUND);
    CHECK(h && call(IAF_M_GET, h) == IAF_M_OK && rs()->state.kin.yaw == IAF_FD_YAW_UNSET);
    CHECK(rs()->state.landing.phase == PH_INBOUND && rs()->abi == 1 && rs()->size == 320);
    CHECK(call(IAF_M_GET, h ^ 0x100u) == IAF_M_BAD_HANDLE && call(IAF_M_GET, 0) == IAF_M_BAD_HANDLE);
    CHECK(call(IAF_M_GET, (h & ~0xffu) | 255u) == IAF_M_BAD_HANDLE);
    memset(rq(), 0, sizeof(*rq()));
    CHECK(iaf_module_dispatch(343) == IAF_M_BAD_SIZE && rs()->status == IAF_M_BAD_SIZE);
    rq()->abi = 2; rq()->size = sizeof(*rq()); rq()->op = IAF_M_GET; rq()->handle = h;
    CHECK(iaf_module_dispatch(sizeof(*rq())) == IAF_M_BAD_SIZE);
    CHECK(call(999, h) == IAF_M_BAD_OP);

    rq()->a[0] = 30; rq()->a[1] = -2; rq()->a[2] = 40; rq()->flag = 0;
    CHECK(call(IAF_M_SEED, h) == 0 && rs()->result == 1 && rs()->state.kin.speed == 50);
    memset(&rq()->landing, 0, sizeof(rq()->landing));
    double a[3] = {100, 20, -100}, b[3] = {100, 20, 1400};
    memcpy(rq()->a, a, sizeof a); memcpy(rq()->b, b, sizeof b); rq()->flag = 0;
    CHECK(call(IAF_M_RUNWAY, 0) == 0 && rs()->result == 1 && rs()->runway.len == 1500);
    rq()->landing.runway = rs()->runway;
    rq()->landing.slope = 0.25; rq()->landing.h0 = 20; rq()->landing.cruise = 250;
    rq()->landing.runway_granted = 1;
    iaf_runway_point(&rq()->landing.runway, 2000, 0, rq()->landing.pos);
    rq()->landing.pos[1] += 250;
    CHECK(call(IAF_M_GET, h) == 0);
    rq()->state = rs()->state; rq()->state.kin.yaw = 0; rq()->state.kin.speed = 70;
    CHECK(call(IAF_M_SET_STATE, h) == 0);
    CHECK(call(IAF_M_LANDING, h) == 0 && rs()->landing.action == IAF_LAND_RETURN);
    CHECK(rs()->state.landing.phase == PH_FINAL);
    CHECK(call(IAF_M_GET, h) == 0 && rs()->state.landing.phase == PH_INBOUND); /* staged only */
    rq()->state.landing.phase = 99;
    CHECK(call(IAF_M_SET_STATE, h) == IAF_M_BAD_STATE);
    rq()->state = rs()->state;
    rq()->state.kin.speed = NAN; rq()->state.kin.vz = 3;
    CHECK(call(IAF_M_SET_STATE, h) == 0);
    rq()->args[0] = 0; rq()->args[1] = 0.1; rq()->args[2] = 0;
    CHECK(call(IAF_M_TAKEOFF_ROLL, h) == 0 && rs()->result == 0 && isnan(rs()->state.kin.speed) && rs()->state.kin.vz == 3);
    rq()->now = 1000; rq()->prev = 1000; rq()->freq = 1000; rs()->values[0] = 7;
    CHECK(call(IAF_M_TICK_DT, 0) == 0 && rs()->result == 0 && rs()->values[0] == 0);
    rq()->now = INT64_MIN; rq()->prev = 1;
    CHECK(call(IAF_M_TICK_DT, 0) == IAF_M_BAD_STATE);
    rq()->now = INT64_MAX; rq()->prev = -1;
    CHECK(call(IAF_M_TICK_DT, 0) == IAF_M_BAD_STATE);
    rq()->args[0] = NAN; rq()->args[1] = 0; rq()->args[2] = 1;
    CHECK(call(IAF_M_CLAMP, 0) == 0 && isnan(rs()->values[0]));

    CHECK(call(IAF_M_GET, h) == 0);
    iaf_module_state before = rs()->state;
    const int32_t invalid_retries[] = {INT32_MAX, INT32_MIN, -1, 3};
    for (unsigned n = 0; n < sizeof(invalid_retries)/sizeof(invalid_retries[0]); ++n) {
        rq()->state = before;
        rq()->state.landing.go_arounds = invalid_retries[n];
        CHECK(call(IAF_M_SET_STATE, h) == IAF_M_BAD_STATE);
        CHECK(call(IAF_M_GET, h) == 0 && !memcmp(&before, &rs()->state, sizeof before));
    }
    rq()->state = before; rq()->state.landing.phase = PH_FINAL;
    rq()->state.landing.go_arounds = 2;
    rq()->state.kin.yaw = 0; rq()->state.kin.speed = 70;
    CHECK(call(IAF_M_SET_STATE, h) == 0);
    iaf_runway_point(&rq()->landing.runway, 250, 61, rq()->landing.pos);
    rq()->landing.pos[1] += 50;
    CHECK(call(IAF_M_LANDING, h) == 0 && rs()->landing.action == IAF_LAND_ABORT);
    CHECK(rs()->state.landing.go_arounds == 3);
    rq()->state = rs()->state;
    CHECK(call(IAF_M_SET_STATE, h) == IAF_M_BAD_STATE);
    CHECK(call(IAF_M_GET, h) == 0 && rs()->state.landing.go_arounds == 2);

    uint32_t all[IAF_MODULE_CAPACITY] = {h};
    for (unsigned i = 1; i < IAF_MODULE_CAPACITY; ++i) CHECK((all[i] = iaf_module_create(PH_HOLD)) != 0);
    CHECK(!iaf_module_create(PH_HOLD));
    CHECK(iaf_module_drop(h) == 0 && iaf_module_drop(h) == IAF_M_BAD_HANDLE && call(IAF_M_GET, h) == IAF_M_BAD_HANDLE);
    uint32_t again = iaf_module_create(PH_HOLD);
    CHECK(again && (again & 0xffu) == (h & 0xffu) && again != h);
    CHECK(iaf_module_reset() == 0 && call(IAF_M_GET, again) == IAF_M_BAD_HANDLE);
    CHECK(iaf_module_create(PH_HOLD) != again);
    if (fails) { fprintf(stderr, "module_test: %d failure(s)\n", fails); return 1; }
    puts("module_test: ok");
    return 0;
}
