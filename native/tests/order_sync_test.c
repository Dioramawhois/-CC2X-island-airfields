/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual production handler against copied game/API values. No DLL, Lua or game runs. */
#ifdef NDEBUG
#error Order regressions require active assertions
#endif
#include <assert.h>
#include "../plugin/island_airfields.c"

static int vehicle, tile, island, airfield, server;
static int scenario, has_wp, wp_type, wp_param, dock, sync_count, sequence;
enum { ACCEPT, REJECT, PENDING };

static void *test_server(void) { return &server; }
static int64_t test_geti(const void *object, int field, int index) {
    (void)index;
    if (object == &vehicle) {
        switch (field) {
        case CC2X_VEHICLE_ID: return 17;
        case CC2X_VEHICLE_IS_AIR: return 1;
        case CC2X_VEHICLE_TEAM: return 1;
        case CC2X_VEHICLE_DOCK_STATE: return dock;
        default: return 0;
        }
    }
    if (object == &tile && field == CC2X_TILE_TEAM) return scenario == REJECT ? 2 : 1;
    if (object == &has_wp && field == CC2X_WAYPOINT_TYPE) return wp_type;
    if (object == &has_wp && field == CC2X_WAYPOINT_PARAM) return wp_param;
    return 0;
}
static int test_seti(void *object, int field, int index, int64_t value) {
    (void)index;
    assert(object == &has_wp);
    if (field == CC2X_WAYPOINT_TYPE) wp_type = (int)value;
    else if (field == CC2X_WAYPOINT_PARAM) wp_param = (int)value;
    else assert(0);
    return 1;
}
static void *test_find(int list, uint32_t id) {
    if (list == CC2X_WORLD_TILES && id == 7) return &tile;
    if (list == CC2X_WORLD_VEHICLES && id == 17) return &vehicle;
    return NULL;
}
static void *test_ref(const void *object, int list, int index) {
    (void)index;
    if (object == &vehicle && list == CC2X_VEHICLE_WAYPOINTS) return has_wp ? &has_wp : NULL;
    if (object == &tile && list == CC2X_TILE_ISLAND) return scenario == PENDING ? NULL : &island;
    if (object == &island && list == CC2X_ISLAND_AIRFIELDS) return &airfield;
    return NULL;
}
static int test_count(const void *object, int list) {
    return object == &island && list == CC2X_ISLAND_AIRFIELDS ? 1 : 0;
}
static int test_request(void *object, void *v) {
    assert(object == &island && v == &vehicle);
    assert(sequence++ == 0);
    dock = CC2X_DOCK_QUEUED;
    return 1;
}
static int test_clear(void *v, int list) {
    assert(v == &vehicle && list == CC2X_VEHICLE_WAYPOINTS);
    assert(sequence++ == 1);
    has_wp = 0;
    return 1;
}
static void test_sync(void *v) {
    assert(v == &vehicle);
    if (sequence) assert(dock == CC2X_DOCK_QUEUED && !has_wp && sequence++ == 2);
    else assert(!has_wp || wp_type == CC2X_WAYPOINT_MOVE);
    ++sync_count;
}
/* The audited game hook precedes the handler's unconditional broadcast.
 * Model that continuation too; testing the callback alone misses duplicates. */
static void incoming_order(void) {
    int before = sync_count;
    on_waypoints(&vehicle, NULL);
    assert(sync_count == before);
    test_sync(&vehicle);
}
static void reset(int value) {
    scenario = value; has_wp = 1; wp_type = CC2X_WAYPOINT_BARGE_LOAD_TILE; wp_param = 7;
    dock = CC2X_DOCK_UNDOCKED; sync_count = sequence = 0;
    g_on = g_planes = 1; g_tick = 0;
    memset(g_pending, 0, sizeof(g_pending));
}
int main(void) {
    cc2x_api api = {0};
    api.server = test_server; api.geti = test_geti; api.seti = test_seti;
    api.find = test_find; api.ref = test_ref; api.count = test_count;
    api.request_landing = test_request; api.list_clear = test_clear; api.waypoints_sync = test_sync;
    A = &api;
    InitializeCriticalSection(&g_table_cs);
    reset(ACCEPT); incoming_order();
    assert(sync_count == 1 && sequence == 3);
    reset(REJECT); incoming_order();
    assert(sync_count == 1 && wp_type == CC2X_WAYPOINT_MOVE && wp_param == 0);
    reset(PENDING); incoming_order();
    assert(sync_count == 1 && g_pending[0].vid == 17);
    scenario = ACCEPT; sequence = sync_count = 0; g_tick = 7;
    on_tick(&server, NULL);
    assert(sync_count == 1 && sequence == 3 && !g_pending[0].vid);
    reset(ACCEPT); land_post(&server, (void *)(uintptr_t)((17u << 16) | 7u));
    assert(sync_count == 1 && sequence == 3);
    reset(ACCEPT); wp_type = CC2X_WAYPOINT_MOVE;
    incoming_order(); assert(sync_count == 1 && wp_param == 7);
    /* A new order immediately cancels a deferred landing, including the same tick. */
    reset(PENDING); incoming_order();
    assert(g_pending[0].vid == 17);
    incoming_order(); /* the player submits a new ordinary move */
    assert(!g_pending[0].vid);
    scenario = ACCEPT; sequence = sync_count = 0; g_tick = 7;
    on_tick(&server, NULL);
    assert(sync_count == 0 && sequence == 0 && dock == CC2X_DOCK_UNDOCKED);
    reset(PENDING); incoming_order(); has_wp = 0;
    incoming_order(); assert(!g_pending[0].vid);
    /* Exhaustion is a rejection, never a falsely reported pending request. */
    reset(PENDING);
    for (int i = 0; i < MAX_PENDING; ++i) {
        g_pending[i].vid = 100u + (uint32_t)i; g_pending[i].tid = 7;
    }
    uint32_t rejected = D.stats[ST_LAND_REJECTED], pending = D.stats[ST_LAND_PENDING];
    assert(land(&vehicle, 7, 1) == 0 && D.stats[ST_LAND_REJECTED] == rejected + 1);
    assert(D.stats[ST_LAND_PENDING] == pending);
    for (int i = 0; i < MAX_PENDING; ++i) assert(g_pending[i].vid == 100u + (uint32_t)i);
    g_pending[5].vid = 17;
    assert(land(&vehicle, 7, 1) == 2 && g_pending[5].vid == 17); /* own slot can be replaced */
    for (int i = 0; i < MAX_PENDING; ++i) if (i != 5) assert(g_pending[i].vid == 100u + (uint32_t)i);
    /* A rejected new landing cannot revive the prior request. */
    reset(PENDING); incoming_order(); scenario = REJECT;
    wp_type = CC2X_WAYPOINT_BARGE_LOAD_TILE; incoming_order();
    assert(!g_pending[0].vid);
    /* Session end/start and lost island ownership invalidate deferred work. */
    reset(PENDING); incoming_order();
    on_session(&server, 0, NULL); on_session(&server, 1, NULL);
    assert(!g_pending[0].vid);
    scenario = ACCEPT; sequence = sync_count = 0; g_tick = 7; on_tick(&server, NULL);
    assert(sync_count == 0 && sequence == 0);
    reset(PENDING); incoming_order();
    scenario = REJECT; g_tick = 7; on_tick(&server, NULL);
    assert(!g_pending[0].vid);
    scenario = ACCEPT; sequence = sync_count = 0; g_tick = 15; on_tick(&server, NULL);
    assert(sync_count == 0 && sequence == 0);
    DeleteCriticalSection(&g_table_cs);
    puts("PASS: final order sync, immediate cancellation, queue exhaustion/replacement, session reset and lost ownership (fake engine)");
    return 0;
}
