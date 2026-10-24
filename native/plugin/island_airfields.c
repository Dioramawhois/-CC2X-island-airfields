/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Island Airfields - CC2X plugin (cc2x_island_airfields.dll).
 *
 * Captured islands get working airfields: aircraft land, park in the hangars, are repaired,
 * refuelled and rearmed by the island, and take off again on a new order.
 *
 * Why it is needed: the airfield logic lives in the island's AI controller (tile+0x100), which the
 * game only creates for AI teams. The plugin lets the game create it for human islands too (S1/S2),
 * skips the garrison spawner there (S3), accepts landing orders (H1, a waypoint of type
 * barge_load_tile carrying the tile id, or cc2x.island_airfields.land from Lua), launches parked
 * aircraft on a new order (H1), assigns the hangar slot on landing (H4, vanilla never writes it) and
 * flies every player aircraft that lands on or takes off from an island airfield itself (H5: the
 * flight director - the vanilla island airfield code is unfinished for player aircraft).
 *
 * Installed as part of the mod: <mod folder>\content\cc2x\cc2x_island_airfields.dll, loaded by CC2X 0.3+
 * when the mod is enabled in the game's mod menu. No settings are needed; optionally
 * %APPDATA%\Carrier Command 2\cc2x\island_airfields.ini
 *     [island_airfields]
 *     planes=1                ; 0 = helicopters only
 *     plane_assist=1          ; 0 = leave planes to the vanilla airfield code (they crash)
 *
 * Tested (v44, save "Test 1"): takeoff + landing of light/heavy planes and helicopters on three
 * islands, two planes sharing one runway, landings resumed after loading a save, cancel by a new
 * order (native/tools/fd_test.py).
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cc2x.h"

#define PLUGIN "island_airfields"
#define VERSION 44

#define THISCALL __attribute__((thiscall))
#define U32(p, o) (*(uint32_t *)((char *)(p) + (o)))
#define I32(p, o) (*(int32_t *)((char *)(p) + (o)))
#define F32(p, o) (*(float *)((char *)(p) + (o)))
#define F64(p, o) (*(double *)((char *)(p) + (o)))
#define PTR(p, o) (*(char **)((char *)(p) + (o)))
#define U8(p, o) (*(uint8_t *)((char *)(p) + (o)))

/* ---- game (1.5.18-win32, image VAs) ---------------------------------------------------------- */
enum {
    F_TILE_BY_ID = 0x00631240, F_TILE_AT_POS = 0x00631040, F_VEHICLE_BY_ID = 0x005ee6a0,
    F_ENQUEUE = 0x0062b4a0, F_WAYPOINTS_CLEAR = 0x00854870, F_DEQUE_GROW = 0x00860c90,
    F_BUILD_TAXI_PATH = 0x00627d70, VT_NP_RIGID_DYNAMIC = 0x00cdd478,
    H1_SITE = 0x007781d4, H4_SITE = 0x0062ae8d, H4_FOUND = 0x0062aefc, H4_NOT_FOUND = 0x0062aead,
    S1_SITE = 0x0063014f, S2_SITE = 0x00634981, S3_SITE = 0x00634f3d, H5_SITE = 0x0062b8f3,
};
/* server */
enum { L_VEHICLES = 0x344, L_TILES = 0x42c, L_TILE_DEQUE = 0x440, L_BROADCAST = 0x514 };
/* vehicle */
enum {
    V_ID = 0x80, V_BODIES = 0x8c, V_WHEELS = 0xac, V_TEAM = 0x108, V_ATTACHED = 0x10c,
    V_DOCK_VEHICLE = 0x114, V_DOCK_TILE = 0x118, V_DOCK_AIRFIELD = 0x11c, V_WAYPOINTS = 0x140,
    V_ATTACH_DATA = 0x15c, V_ATTACH_COUNT = 0x160, V_OP = 0x170, V_DOCK_STATE = 0x174,
    V_TEAM_OBJECT = 0x1a8, V_CTRL_MODE = 0x1d0, V_CTRL_TARGET = 0x1d8, V_CTRL_SPEED = 0x1f8,
    V_CTRL_FACTOR = 0x1fc, V_CTRL_TAXI = 0x200,
    VT_IS_AIR = 0x44, VT_IS_WING = 0x48, VT_IS_ROTOR = 0x4c, VT_SET_BRAKES = 0x94, VT_SET_GEAR = 0x98,
    BODY_ACTOR = 0xf0, BODY_POS = 0x48, TEAM_IS_AI = 0x19,
};
enum { DOCK_UNDOCKED = 0, DOCK_PENDING_UNDOCK = 2, DOCK_DOCKED = 4, DOCK_QUEUE = 5, DOCK_TAXI = 6 };
enum { WP_TYPE = 0x04, WP_PARAM = 0x20, WP_MOVE = 0, WP_LAND = 4 };
/* tile / island / airfield */
enum { T_ID = 0, T_TEAM = 0x60, T_SAVED_COUNT = 0xcc, T_SAVED_SET = 0xd0, T_ISLAND = 0x100, I_AIRFIELDS = 0x154 };
enum {
    A_ENV = 0x00, A_XFORM = 0x08, A_HANGARS = 0x68, A_HANGAR_COUNT = 0x6c, A_SELECTED = 0x70,
    A_MODE = 0x74, A_STATE = 0x78, A_RUNWAY_VEHICLE = 0x7c, A_END_NODE = 0xb4, A_START_NODE = 0xb8,
    A_PATH = 0xbc, A_DOCK_QUEUE = 0xcc, A_UNDOCK_QUEUE = 0xdc, A_TILE = 0xf0, ENV_NODES = 0x180, NODE_POS = 0x34,
};

typedef void *(THISCALL *ByIdFn)(void *self, uint32_t id);
typedef void *(THISCALL *AtPosFn)(void *self, double *pos);
typedef void (THISCALL *Vehicle1Fn)(void *self, void *vehicle);
typedef void (THISCALL *VoidFn)(void *self);
typedef void (THISCALL *GrowFn)(void *self, uint32_t cap);
typedef void (THISCALL *BroadcastFn)(void *self, uint32_t id, void *a, void *b, uint32_t c);
typedef char (THISCALL *BoolFn)(void *self);
typedef char (THISCALL *IsAiFn)(void *self, int team);
typedef int (THISCALL *SpawnerFn)(void *self, int a1, int a2, void *team, int a4, int a5);
typedef void (THISCALL *AfTickFn)(void *self, int a, int b, int c);
typedef void (THISCALL *TaxiPathFn)(void *self, uint32_t hangar, void *path);
typedef void (THISCALL *BrakesFn)(void *self, float brake, float grip);
typedef void (THISCALL *GearFn)(void *self, int down);
typedef float *(THISCALL *GetVelFn)(void *self, float *out);
typedef void (THISCALL *SetVelFn)(void *self, const float *v, int wake);

static const cc2x_api *A;
static SpawnerFn o_spawner;
static AfTickFn o_af_tick;
static int g_planes;          /* fixed-wing aircraft may land on islands */
static int g_plane_assist;    /* our glide / arrester / slow taxi for them (0 = pure vanilla) */
static uint32_t g_tick;

/* ---- debug / test channel (read by native/tools, exported) ----------------------------------- */
enum {
    ST_LAND_REQUESTS, ST_LAND_QUEUED, ST_LAND_REJECTED, ST_LAND_PENDING, ST_TAKEOFF_REQUESTS,
    ST_TAKEOFF_QUEUED, ST_TAKEOFF_REJECTED, ST_HANGAR_ASSIGNED, ST_HANGAR_CLEANED, ST_ISLANDS_ENABLED,
    ST_SPAWNS_SKIPPED, ST_TABLE_REFRESH, ST_LUA_QUERIES, ST_SERVER_TICKS, ST_GLIDE_FIXES, ST_TAXI_FORCED,
    ST_ARREST_TICKS, ST_ROLLOUTS, ST_LAND_CANCELLED, ST_COUNT
};
typedef struct {
    uint32_t magic, version;
    volatile uint32_t stats[32];
    volatile int32_t cmd, a, b, c, d;      /* test command for the Lua screen (consumed by Lua) */
    volatile uint32_t watch_id;
    volatile uint32_t watch_ptr;
    volatile uint32_t server;
    double dbg[8];                         /* dist, lx, vanilla_y, target_y, speed, height, taxi_dist */
    volatile int32_t scan_tile;            /* tools: map an airfield with rays -> <game>\iaf_scan_<tile>.txt */
} Debug;
CC2X_EXPORT Debug cc2x_island_airfields_debug = {.magic = 0x44464149 /* 'IAFD' */, .version = VERSION};
#define D cc2x_island_airfields_debug
#define STAT(x) (D.stats[ST_##x]++)

/* ---- helpers --------------------------------------------------------------------------------- */
typedef struct { uint32_t data, cap, head, count; } Deque;

static uint32_t dq_index(const void *dq, uint32_t i) {
    const Deque *q = dq;
    uint32_t idx = q->head + i;
    return idx >= q->cap ? idx - q->cap : idx;
}
static void *dq_ptr(const void *dq, uint32_t i) {
    const Deque *q = dq;
    if (!q->data || i >= q->count) return NULL;
    return ((void **)(uintptr_t)q->data)[dq_index(dq, i)];
}

static void *vt(void *obj, int off) { return *(void **)(*(char **)obj + off); }
static int is_air(void *v) { return ((BoolFn)vt(v, VT_IS_AIR))(v) != 0; }
static int is_wing(void *v) { return ((BoolFn)vt(v, VT_IS_WING))(v) != 0; }
static int is_human(void *v) {
    char *team = PTR(v, V_TEAM_OBJECT);
    return team && !U8(team, TEAM_IS_AI);
}
static int can_land(void *v) { return is_air(v) && (g_planes || !is_wing(v)); }

static void *vehicle_by_id(void *srv, uint32_t id) { return ((ByIdFn)A->va(F_VEHICLE_BY_ID))((char *)srv + L_VEHICLES, id); }
static void *tile_by_id(void *srv, uint32_t id) { return ((ByIdFn)A->va(F_TILE_BY_ID))((char *)srv + L_TILES, id); }

static void *first_body(void *v) { return dq_ptr((char *)v + V_BODIES, 0); }
static void *first_waypoint(void *v) {
    Deque *q = (Deque *)((char *)v + V_WAYPOINTS);
    if (!q->count || !q->data) return NULL;
    return (char *)(uintptr_t)q->data + 0x40 * dq_index(q, 0);
}
static uint32_t airfield_count(void *island) { return U32(island, I_AIRFIELDS + 0xc); }
static void *airfield_at(void *island, uint32_t i) { return dq_ptr((char *)island + I_AIRFIELDS, i); }

static void broadcast_waypoints(void *srv, void *v) {
    void *obj = (char *)srv + L_BROADCAST;
    ((BroadcastFn)vt(obj, 0))(obj, U32(v, V_ID), (char *)v + 0x230, (char *)v + V_WAYPOINTS, U32(v, 0x150));
}

/* ---- pending landings (island not loaded yet) ------------------------------------------------ */
#define MAX_PENDING 32
static struct { uint32_t vid, tid, tick; } g_pending[MAX_PENDING];

static void pending_remove(uint32_t vid, int only_old) {
    for (int i = 0; i < MAX_PENDING; ++i)
        if (g_pending[i].vid == vid && (!only_old || g_tick - g_pending[i].tick > 30)) g_pending[i].vid = 0;
}
static void pending_add(uint32_t vid, uint32_t tid) {
    pending_remove(vid, 0);
    for (int i = 0; i < MAX_PENDING; ++i)
        if (!g_pending[i].vid) { g_pending[i].vid = vid; g_pending[i].tid = tid; g_pending[i].tick = g_tick; return; }
}

/* ---- hangars --------------------------------------------------------------------------------- */
/* free slots whose vehicle no longer exists or is flying */
static void cleanup_hangars(void *srv, void *island) {
    for (uint32_t i = 0; i < airfield_count(island); ++i) {
        char *af = airfield_at(island, i);
        uint32_t *h = (uint32_t *)PTR(af, A_HANGARS);
        if (!af || !h) continue;
        for (uint32_t k = 0; k < U32(af, A_HANGAR_COUNT); ++k) {
            if (!h[k]) continue;
            void *v = vehicle_by_id(srv, h[k]);
            if (!v || U32(v, V_DOCK_STATE) == DOCK_UNDOCKED) { h[k] = 0; STAT(HANGAR_CLEANED); }
        }
    }
}
static int hangar_has(void *af, uint32_t id) {
    uint32_t *h = (uint32_t *)PTR(af, A_HANGARS);
    for (uint32_t k = 0; h && k < U32(af, A_HANGAR_COUNT); ++k)
        if (h[k] == id) return 1;
    return 0;
}
static int hangar_put(void *af, uint32_t id) {
    uint32_t *h = (uint32_t *)PTR(af, A_HANGARS);
    if (!h || hangar_has(af, id)) return h != NULL;
    for (uint32_t k = 0; k < U32(af, A_HANGAR_COUNT); ++k)
        if (!h[k]) { h[k] = id; STAT(HANGAR_ASSIGNED); return 1; }
    return 0;
}
static void *find_parked_airfield(void *island, uint32_t id) {
    for (uint32_t i = 0; i < airfield_count(island); ++i) {
        void *af = airfield_at(island, i);
        if (af && hangar_has(af, id)) return af;
    }
    return NULL;
}

/* ---- landing / takeoff ----------------------------------------------------------------------- */
static int do_land(void *srv, void *v, void *island) {
    cleanup_hangars(srv, island);
    ((Vehicle1Fn)A->va(F_ENQUEUE))((char *)island + I_AIRFIELDS, v);
    if (U32(v, V_DOCK_STATE) != DOCK_QUEUE) return 0;
    ((VoidFn)A->va(F_WAYPOINTS_CLEAR))((char *)v + V_WAYPOINTS);
    return 1;
}

/* 1 queued, 0 rejected, 2 pending (island not loaded) */
static int land(void *srv, void *v, uint32_t tile_id) {
    STAT(LAND_REQUESTS);
    pending_remove(U32(v, V_ID), 0);
    if (!can_land(v) || U32(v, V_DOCK_STATE) != DOCK_UNDOCKED || U32(v, V_ATTACHED) || U32(v, V_DOCK_VEHICLE)) {
        STAT(LAND_REJECTED);
        return 0;
    }
    char *tile = tile_by_id(srv, tile_id);
    if (!tile || U32(tile, T_TEAM) != U32(v, V_TEAM)) { STAT(LAND_REJECTED); return 0; }
    void *island = PTR(tile, T_ISLAND);
    if (!island) { pending_add(U32(v, V_ID), tile_id); STAT(LAND_PENDING); return 2; }
    if (!airfield_count(island) || !do_land(srv, v, island)) { STAT(LAND_REJECTED); return 0; }
    STAT(LAND_QUEUED);
    return 1;
}

static int push_undock(char *af, uint32_t id) {
    if (I32(af, A_RUNWAY_VEHICLE) == (int32_t)id) return 0;
    for (int qi = 0; qi < 2; ++qi) {
        Deque *q = (Deque *)(af + (qi ? A_DOCK_QUEUE : A_UNDOCK_QUEUE));
        for (uint32_t i = 0; i < q->count; ++i)
            if (((uint32_t *)(uintptr_t)q->data)[dq_index(q, i)] == id) return 0;
    }
    Deque *q = (Deque *)(af + A_UNDOCK_QUEUE);
    if (q->count == q->cap) ((GrowFn)A->va(F_DEQUE_GROW))(q, q->cap * 2 < 0x20 ? 0x20 : q->cap * 2);
    ((uint32_t *)(uintptr_t)q->data)[dq_index(q, q->count)] = id;
    q->count++;
    return 1;
}

static void takeoff(void *srv, void *v, char *wp) {
    STAT(TAKEOFF_REQUESTS);
    char *body = first_body(v);
    char *tile = body ? ((AtPosFn)A->va(F_TILE_AT_POS))((char *)srv + L_TILES, (double *)(body + BODY_POS)) : NULL;
    void *island = tile ? PTR(tile, T_ISLAND) : NULL;
    if (!island || !airfield_count(island)) { STAT(TAKEOFF_REJECTED); return; }
    char *af = find_parked_airfield(island, U32(v, V_ID));
    if (!af) {                       /* parked but not registered: use airfield 0 */
        af = airfield_at(island, 0);
        if (!af) { STAT(TAKEOFF_REJECTED); return; }
        hangar_put(af, U32(v, V_ID));
    }
    if (!push_undock(af, U32(v, V_ID))) return;   /* already queued / on the runway */
    U32(v, V_DOCK_STATE) = DOCK_PENDING_UNDOCK;
    U32(v, V_DOCK_VEHICLE) = 0;
    U32(v, V_DOCK_TILE) = 0;
    I32(v, V_DOCK_AIRFIELD) = -1;
    STAT(TAKEOFF_QUEUED);
    if (wp && U32(wp, WP_TYPE) == WP_LAND) { U32(wp, WP_TYPE) = WP_MOVE; U32(wp, WP_PARAM) = 0; }
}

/* A new order for an aircraft that is queued for / approaching / taxiing on an island airfield
 * cancels that landing, like it does on the carrier. Returns 1 if a landing was cancelled. */
#define EV(...) A->log(PLUGIN, CC2X_LOG_INFO, __VA_ARGS__)
static int fd_cancel(char *v);
static void fd_takeoff(void *srv, char *v, char *wp);

static int cancel_island_landing(void *srv, char *v) {
    uint32_t id = U32(v, V_ID), st = U32(v, V_DOCK_STATE);
    if ((st != 1 && st != DOCK_QUEUE && st != DOCK_TAXI) || U32(v, V_DOCK_VEHICLE) || U32(v, V_ATTACHED)) return 0;
    Deque *tiles = (Deque *)((char *)srv + L_TILE_DEQUE);
    for (uint32_t t = 0; t < tiles->count; ++t) {
        char *tile = dq_ptr(tiles, t);
        char *island = tile ? PTR(tile, T_ISLAND) : NULL;
        for (uint32_t i = 0; island && i < airfield_count(island); ++i) {
            char *af = airfield_at(island, i);
            if (!af) continue;
            int found = 0;
            if (I32(af, A_RUNWAY_VEHICLE) == (int32_t)id) {
                I32(af, A_RUNWAY_VEHICLE) = -1;
                U32(af, A_STATE) = 0;
                U32(af, A_MODE) = 0;
                U32(af, A_PATH + 0xc) = 0;
                found = 1;
            }
            Deque *q = (Deque *)(af + A_DOCK_QUEUE);
            for (uint32_t k = 0; q->data && k < q->count; ++k) {
                uint32_t *items = (uint32_t *)(uintptr_t)q->data;
                if (items[dq_index(q, k)] != id) continue;
                for (uint32_t j = k; j + 1 < q->count; ++j) items[dq_index(q, j)] = items[dq_index(q, j + 1)];
                q->count--;
                found = 1;
                break;
            }
            if (!found) continue;
            uint32_t *h = (uint32_t *)PTR(af, A_HANGARS);
            for (uint32_t k = 0; h && k < U32(af, A_HANGAR_COUNT); ++k) if (h[k] == id) h[k] = 0;
            U32(v, V_DOCK_STATE) = DOCK_UNDOCKED;
            U32(v, V_OP) = 0;
            U32(v, V_CTRL_MODE) = 0;
            U8(v, V_CTRL_TAXI) = 0;
            I32(v, V_DOCK_AIRFIELD) = -1;
            STAT(LAND_CANCELLED);
            EV("vehicle %u: landing cancelled by a new order (tile %u)", id, U32(tile, T_ID));
            return 1;
        }
    }
    return 0;
}

/* H1: end of the server's "set vehicle waypoints" handler. eax = vehicle, esi = server */
static void h1_waypoints(cc2x_cpu *cpu, void *user) {
    void *v = (void *)(uintptr_t)cpu->eax, *srv = (void *)(uintptr_t)cpu->esi;
    if (!v || !srv || !is_air(v) || !is_human(v)) return;
    char *wp = first_waypoint(v);
    if (!wp) { pending_remove(U32(v, V_ID), 1); return; }
    if (U32(v, V_DOCK_STATE) == DOCK_DOCKED) {
        if (!U32(v, V_ATTACHED)) fd_takeoff(srv, v, wp);  /* attached = parked on a carrier */
        return;
    }
    if (U32(wp, WP_TYPE) != WP_LAND) {
        pending_remove(U32(v, V_ID), 1);
        if (!fd_cancel(v)) cancel_island_landing(srv, v);
        return;
    }
    int r = land(srv, v, U32(wp, WP_PARAM));
    if (r != 1) { U32(wp, WP_TYPE) = WP_MOVE; U32(wp, WP_PARAM) = 0; }   /* fly there instead */
}

/* H4: hangar choice on landing. edi = airfield, esi = vehicle */
static void h4_hangar(cc2x_cpu *cpu, void *user) {
    char *af = (char *)(uintptr_t)cpu->edi;
    void *v = (void *)(uintptr_t)cpu->esi;
    uint32_t n = U32(af, A_HANGAR_COUNT), *h = (uint32_t *)PTR(af, A_HANGARS);
    int found = -1;
    if (n && h) {
        if (v && is_human(v)) {     /* the slot of this vehicle, else a free one (vanilla never writes it) */
            uint32_t id = U32(v, V_ID);
            for (uint32_t k = 0; k < n && found < 0; ++k) if (h[k] == id) found = (int)k;
            for (uint32_t k = 0; k < n && found < 0; ++k) if (!h[k]) { h[k] = id; found = (int)k; STAT(HANGAR_ASSIGNED); }
        }
        for (uint32_t k = 0; k < n && found < 0; ++k) if (h[k]) found = (int)k;   /* vanilla rule */
    }
    cpu->edx = n;
    cpu->eax = found < 0 ? 0 : (uint32_t)found;
    cpu->jump_to = A->va(found < 0 ? H4_NOT_FOUND : H4_FOUND);
}

/* S1/S2: "is the island's team AI-controlled?" when the island AI controller is created */
static char THISCALL s_is_ai(void *teams, int team) {
    char r = ((IsAiFn)vt(teams, 0x10))(teams, team);
    if (r) return r;
    D.stats[ST_ISLANDS_ENABLED]++;
    return 1;
}

/* S3: garrison spawner - vanilla never runs it for human islands */
static int THISCALL s_spawner(void *self, int a1, int a2, void *team, int a4, int a5) {
    if (team && U8(team, TEAM_IS_AI)) return o_spawner(self, a1, a2, team, a4, a5);
    STAT(SPAWNS_SKIPPED);
    return 0;
}

/* ---- H5: the flight director --------------------------------------------------------------------
 * The vanilla island airfield code is unfinished for player aircraft: it holds them on a 3.2 km
 * circle until they happen to be at the right angle, flies planes in at 18 m/s down a 22 deg slope
 * (they stall into the ground), only accepts a touchdown sinking slower than 0.5 m/s, and takes
 * off without any heading control (planes leave across the runway). So every human aircraft that
 * lands on or takes off from an island airfield is flown by this plugin from start to end, one
 * explicit phase at a time (every change is logged), with velocity and attitude set on its PhysX
 * body each tick. The game's autopilot gets the same target, so engines and sound keep running.
 *   plane landing:  inbound -> downwind -> base (180 deg turn) -> final -> rollout -> parked
 *   plane takeoff:  lined up on the runway -> takeoff roll -> climb -> handed back to the game
 *   heli landing:   inbound (braking to a hover over its pad) -> descend -> parked
 *   heli takeoff:   put on its pad -> lift -> handed back to the game
 * Parking and lining up move the aircraft between its hangar bay and the runway / pad, like the
 * carrier's elevator. Only one plane uses the runway at a time; helicopters use their own pads. */

static float *node_pos(char *af, uint32_t idx) {
    char *env = PTR(af, A_ENV);
    if (!env) return NULL;
    Deque *q = (Deque *)(env + ENV_NODES);
    uint32_t i = q->head + idx;
    if (i >= q->cap) i -= q->cap;
    if (i >= q->cap || !q->data) return NULL;
    return (float *)((char *)(uintptr_t)q->data + 0x40 * i + NODE_POS);
}

static void node_world(char *af, float *n, double out[3]) {
    double *m = (double *)(af + A_XFORM);
    for (int k = 0; k < 3; ++k) out[k] = n[0] * m[k] + n[1] * m[3 + k] + n[2] * m[6 + k] + m[9 + k];
}

/* any main gear wheel (index >= 1; wheel 0 is the nose wheel) touching the ground */
static int main_gear_down(char *v) {
    char *w = PTR(v, V_WHEELS);
    uint32_t n = U32(v, V_WHEELS + 4);
    for (uint32_t i = 1; w && i < n && i < 8; ++i)
        if (F32(w, 0x70 * i) > 0.01f) return 1;
    return 0;
}

typedef struct { float q[4]; float p[3]; } PxTransform;
typedef PxTransform *(THISCALL *GetPoseFn)(void *self, PxTransform *out);
typedef void (THISCALL *SetPoseFn)(void *self, const PxTransform *pose, int wake);

static char *body_actor(char *v) {
    char *body = first_body(v);
    char *actor = body ? PTR(body, BODY_ACTOR) : NULL;
    return actor && U32(actor, 0) == A->va(VT_NP_RIGID_DYNAMIC) ? actor : NULL;
}

static void freeze(char *actor) {
    static const float zero[3] = {0, 0, 0};
    ((SetVelFn)vt(actor, 39 * 4))(actor, zero, 1);
    ((SetVelFn)vt(actor, 41 * 4))(actor, zero, 1);
}

static double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }
static double wrap(double a) { return remainder(a, 6.283185307179586); }

/* local coordinates of a world point (rows of the airfield transform are orthonormal) */
static void to_world(char *af, const double l[3], double w[3]) {
    double *m = (double *)(af + A_XFORM);
    for (int k = 0; k < 3; ++k) w[k] = l[0] * m[k] + l[1] * m[3 + k] + l[2] * m[6 + k] + m[9 + k];
}

/* ---- terrain: the game's own PhysX scene query (the game calls PxScene vtable +0x158 = raycast the
 * same way). Static actors only: terrain and buildings, not vehicles. */
typedef void *(THISCALL *GetSceneFn)(void *actor);
typedef uint8_t (THISCALL *RaycastFn)(void *scene, const float *origin, const float *dir, float dist, void *hits,
                                      uint32_t hit_flags, const void *filter, void *filter_cb, const void *cache);
static uint8_t THISCALL ray_touches(void *self, const void *hits, uint32_t n) { return 0; }
static void THISCALL ray_finalize(void *self) {}
static void *THISCALL ray_dtor(void *self, uint32_t flags) { return self; }
static void *g_ray_vt[3];                              /* PxHitCallback<PxRaycastHit>: touches, finalize, dtor */

typedef struct { void *scene; double shift[3]; } Probe;   /* PhysX floating origin = world - shift */

static int probe_init(Probe *pr, char *v) {
    char *actor = body_actor(v), *body = first_body(v);
    if (!actor || !body) return 0;
    pr->scene = ((GetSceneFn)vt(actor, 6 * 4))(actor);
    if (!pr->scene) return 0;
    PxTransform pose;
    ((GetPoseFn)vt(actor, 18 * 4))(actor, &pose);
    for (int k = 0; k < 3; ++k) pr->shift[k] = F64(body, 0x48 + 8 * k) - pose.p[k];
    g_ray_vt[0] = (void *)ray_touches; g_ray_vt[1] = (void *)ray_finalize; g_ray_vt[2] = (void *)ray_dtor;
    return 1;
}

/* world height of the first static surface (terrain or building) below x,y0,z; *hit = 0: none */
static double probe_down(Probe *pr, double x, double y0, double z, int *hit) {
    static const float down[3] = {0.0f, -1.0f, 0.0f};
    struct { uint32_t data[4]; uint16_t flags; } filter = {{0, 0, 0, 0}, 1};   /* PxQueryFlag::eSTATIC */
    float o[3] = {(float)(x - pr->shift[0]), (float)(y0 - pr->shift[1]), (float)(z - pr->shift[2])};
    char h[0x54];                                         /* PxRaycastBuffer */
    memset(h, 0, sizeof(h));
    *(void ***)h = g_ray_vt;
    *(uint32_t *)(h + 0x0c) = 0xffffffff;                 /* faceIndex */
    *(float *)(h + 0x2c) = 3.4e38f;                       /* distance */
    ((RaycastFn)vt(pr->scene, 0x158))(pr->scene, o, down, 4000.0f, h, 0x403, &filter, NULL, NULL);
    if (hit) *hit = h[0x44] != 0;
    if (!h[0x44]) return 0.0;
    double y = *(float *)(h + 0x18) + pr->shift[1];
    return y > 0.0 ? y : 0.0;
}
/* world height of the ground (terrain or building) at x,z; *hit = 0 over open sea */
static double probe_ground(Probe *pr, double x, double z, int *hit) { return probe_down(pr, x, 3000.0, z, hit); }

/* ---- runway geometry: rev = 0 lands from the start node towards the end node ------------------ */
typedef struct { double thr[3], ax, az, len; } Rwy;

static int runway(char *af, int rev, Rwy *r) {
    float *s = node_pos(af, U32(af, A_START_NODE)), *e = node_pos(af, U32(af, A_END_NODE));
    if (!s || !e) return 0;
    double ws[3], we[3];
    node_world(af, s, ws);
    node_world(af, e, we);
    double *a = rev ? we : ws, *b = rev ? ws : we;
    double dx = b[0] - a[0], dz = b[2] - a[2], len = sqrt(dx * dx + dz * dz);
    if (len < 1.0) return 0;
    memcpy(r->thr, a, sizeof(r->thr));
    r->ax = dx / len; r->az = dz / len; r->len = len;
    return 1;
}
/* s: distance before the threshold (> 0 on the approach side), lat: distance to the side */
static void rwy_frame(const Rwy *r, double x, double z, double *s, double *lat) {
    double dx = x - r->thr[0], dz = z - r->thr[2];
    *s = -(dx * r->ax + dz * r->az);
    *lat = -dx * r->az + dz * r->ax;
}
static void rwy_point(const Rwy *r, double s, double lat, double out[3]) {
    out[0] = r->thr[0] - s * r->ax - lat * r->az;
    out[1] = r->thr[1];
    out[2] = r->thr[2] - s * r->az + lat * r->ax;
}
static double rwy_yaw(const Rwy *r) { return atan2(r->ax, r->az); }   /* body +z is the nose */

/* ---- approach plan, once per airfield: the terrain around it is scanned. The pattern height clears
 * everything around; of the two runway ends and a few glide slopes the flattest final that clears
 * the ground on the way in is taken, and the takeoff direction with the clearer climb-out. */
#define P_MARGIN 40.0               /* clearance over terrain and buildings */
#define P_CRUISE_MIN 250.0          /* the vanilla holding height */
#define P_SCAN_RADIUS 3500.0
#define P_AIM 60.0                  /* the glide path meets the runway this far past the threshold */
#define P_SLOPE_MIN 0.12            /* ~7 deg */
#define P_SLOPE_MAX 0.40            /* ~22 deg: 25 m/s sink at the final speed */
typedef struct { char *af; int rev, trev; double slope, h0, cruise; } Plan;
static Plan g_plan[16];
static unsigned g_plan_next;

static Plan *plane_plan(char *af, char *v) {
    for (int i = 0; i < 16; ++i) if (g_plan[i].af == af) return &g_plan[i];
    static Plan fallback;
    Plan *pl = &fallback;
    pl->af = NULL; pl->rev = 0; pl->trev = 0; pl->slope = 0.25; pl->h0 = 20.0; pl->cruise = P_CRUISE_MIN;
    Rwy r[2];
    Probe pr;
    if (!runway(af, 0, &r[0]) || !runway(af, 1, &r[1]) || !probe_init(&pr, v)) return pl;
    int hit = 0;
    double ry = r[0].thr[1];
    double g = probe_ground(&pr, r[0].thr[0], r[0].thr[2], &hit);
    if (!hit || fabs(g - ry) > 15.0) return pl;           /* island not in the physics world (yet): retry */

    double *m = (double *)(af + A_XFORM), top = 0.0;
    for (double dx = -P_SCAN_RADIUS; dx <= P_SCAN_RADIUS; dx += 125.0)
        for (double dz = -P_SCAN_RADIUS; dz <= P_SCAN_RADIUS; dz += 125.0) {
            if (dx * dx + dz * dz > P_SCAN_RADIUS * P_SCAN_RADIUS) continue;
            double gr = probe_ground(&pr, m[9] + dx, m[11] + dz, NULL) - ry;
            if (gr > top) top = gr;
        }

    /* landing: the glide path is one straight line down to the touchdown point P_AIM m past the
     * threshold; its slope is the flattest one that clears the ground under the final and 80 m to
     * either side of it (clearance P_MARGIN, less close to the runway). Takeoff: a 0.2 climb from the
     * middle of the runway clearing the ground beyond its end. */
    double req[2], out[2];
    for (int rev = 0; rev < 2; ++rev) {
        req[rev] = 0.0;
        out[rev] = -1e9;
        for (double d = 50.0; d <= 4000.0; d += 25.0)
            for (double lat = -80.0; lat <= 80.0; lat += 80.0) {
                double p[3];
                rwy_point(&r[rev], d, lat, p);
                double gr = probe_ground(&pr, p[0], p[2], NULL) - ry + fmin(P_MARGIN, 8.0 + 0.15 * d);
                if (gr / (d + P_AIM) > req[rev]) req[rev] = gr / (d + P_AIM);
                rwy_point(&r[rev], -r[rev].len - d, lat, p);
                gr = probe_ground(&pr, p[0], p[2], NULL) - ry + P_MARGIN - 0.2 * (d + r[rev].len * 0.5);
                if (gr > out[rev]) out[rev] = gr;
            }
    }
    int best_rev = req[1] < req[0] ? 1 : 0;
    double slope = clampd(req[best_rev], P_SLOPE_MIN, P_SLOPE_MAX);
    if (req[best_rev] > P_SLOPE_MAX)
        EV("tile %u: the terrain needs a %.2f glide slope, more than %.2f", U32(af, A_TILE), req[best_rev], P_SLOPE_MAX);

    pl = &g_plan[g_plan_next++ % 16];
    pl->af = af;
    pl->rev = best_rev;
    pl->trev = out[1] < out[0] ? 1 : 0;
    pl->slope = slope;
    pl->h0 = slope * P_AIM;
    pl->cruise = top + 100.0 > P_CRUISE_MIN ? top + 100.0 : P_CRUISE_MIN;
    EV("tile %u plan: land from the %s end, slope %.2f, %.0f m over the threshold; take off from the %s end; "
       "pattern %.0f m (highest ground %.0f m)", U32(af, A_TILE), best_rev ? "far" : "vanilla", pl->slope, pl->h0,
       pl->trev ? "far" : "vanilla", pl->cruise, top);
    return pl;
}

/* Hangar parking bays (same airfield prefab everywhere; measured from the aircraft the game parks
 * in AI hangars): slot 0 (-20,-20), 1 (+20,-20), 2 (-20,+20), 3 (+20,+20) in airfield coordinates,
 * nose towards the taxi lane in the middle (slots 0/2 face +x, 1/3 face -x). */
static void bay_world(char *af, uint32_t slot, double w[3], double *yaw) {
    double l[3] = {(slot & 1) ? 20.0 : -20.0, 0.0, (slot & 2) ? 20.0 : -20.0};
    to_world(af, l, w);
    double *m = (double *)(af + A_XFORM), sgn = (slot & 1) ? -1.0 : 1.0;
    *yaw = atan2(sgn * m[0], sgn * m[2]);
}
/* helicopter pads on the open taxiway between the hangars (z -20..20) and the runway (z -140) */
static void pad_world(char *af, uint32_t slot, double w[3]) {
    double l[3] = {(slot & 1) ? 12.0 : -12.0, 0.0, -70.0 - ((slot & 2) ? 25.0 : 0.0)};
    to_world(af, l, w);
}

static double af_distance(char *af, char *v) {
    char *body = first_body(v);
    if (!body) return 1e9;
    double *m = (double *)(af + A_XFORM);
    double dx = F64(body, 0x48) - m[9], dz = F64(body, 0x58) - m[11];
    return sqrt(dx * dx + dz * dz);
}

static int hangar_slot(char *af, uint32_t id) {
    uint32_t n = U32(af, A_HANGAR_COUNT), *h = (uint32_t *)PTR(af, A_HANGARS);
    for (uint32_t k = 0; h && k < n; ++k) if (h[k] == id) return (int)k;
    for (uint32_t k = 0; h && k < n; ++k) if (!h[k]) { h[k] = id; STAT(HANGAR_ASSIGNED); return (int)k; }
    return -1;
}
static void hangar_release(char *af, uint32_t id) {
    uint32_t *h = (uint32_t *)PTR(af, A_HANGARS);
    for (uint32_t k = 0; h && k < U32(af, A_HANGAR_COUNT); ++k) if (h[k] == id) h[k] = 0;
}

/* ---- vehicle flags as the vanilla airfield code sets them --------------------------------------- */
static void attach_flags(char *v, uint8_t flying) {
    for (uint32_t i = 0; i < U32(v, V_ATTACH_COUNT); ++i) {
        char *att = ((char **)PTR(v, V_ATTACH_DATA))[i];
        if (att) U8(att, 0xc0) = flying;
    }
}
static void as_flying(char *v) { U32(v, V_OP) = 2; attach_flags(v, 1); }
static void as_ground(char *v) { U32(v, V_OP) = 1; attach_flags(v, 0); }
static void gear(char *v, int down) { ((GearFn)vt(v, VT_SET_GEAR))(v, down); }

/* ---- the flights -------------------------------------------------------------------------------- */
#define FD_PLANE_RANGE 6000.0       /* landing: taken out of the vanilla holding queue within this range */
#define FD_HELI_RANGE 4000.0
#define P_SPEED 75.0
#define P_FINAL_SPEED 62.0
#define P_TURN_RATE 0.14            /* rad/s (~8 deg/s): a ~540 m turn at P_SPEED */
#define P_WIDTH (2.0 * P_SPEED / P_TURN_RATE)   /* downwind leg: a 180 deg turn ends on the final */
#define P_BANK_SIGN -1.0            /* +x is the left wing: a left turn (yaw rate > 0) lowers it */
#define P_ROTATE_SPEED 45.0
#define P_HANDOFF_HEIGHT 120.0
#define H_CRUISE 100.0
#define H_SPEED 45.0
#define H_HANDOFF_HEIGHT 40.0

enum {
    PH_NONE, PH_HOLD, PH_INBOUND, PH_DOWNWIND, PH_BASE, PH_FINAL, PH_ROLLOUT,
    PH_WAIT, PH_ROLL, PH_CLIMB, PH_H_INBOUND, PH_H_DESCEND, PH_H_LIFT
};
static const char *PH_NAME[] = {"-", "hold", "inbound", "downwind", "base turn", "final", "rollout",
                                "waiting for the runway", "takeoff roll", "climb", "inbound", "descend", "lift"};

typedef struct {
    uint32_t vid, slot, t0;
    char *af;
    int phase, takeoff, side, lined, gear_set, go_arounds;
    double yaw, speed, vz, roll, stand, hv[2];
    uint32_t t_desc;
    double max_lat, min_clear, td_speed;
    LARGE_INTEGER t;
} Flight;
#define MAX_FLIGHTS 32
static Flight g_fl[MAX_FLIGHTS];
static struct { char *af; uint32_t vid; } g_lock[16];   /* one plane on a runway at a time */

static int lock_take(char *af, uint32_t vid) {
    int free_i = -1;
    for (int i = 0; i < 16; ++i) {
        if (g_lock[i].af == af) return g_lock[i].vid == vid;
        if (!g_lock[i].af && free_i < 0) free_i = i;
    }
    if (free_i < 0) return 0;
    g_lock[free_i].af = af; g_lock[free_i].vid = vid;
    return 1;
}
static void lock_free(char *af, uint32_t vid) {
    for (int i = 0; i < 16; ++i) if (g_lock[i].af == af && g_lock[i].vid == vid) g_lock[i].af = NULL;
}

static Flight *fl_find(uint32_t vid) {
    for (int i = 0; i < MAX_FLIGHTS; ++i) if (g_fl[i].vid == vid) return &g_fl[i];
    return NULL;
}
static Flight *fl_new(uint32_t vid, char *af, uint32_t slot, int takeoff, int phase) {
    Flight *f = fl_find(vid);
    for (int i = 0; i < MAX_FLIGHTS && !f; ++i) if (!g_fl[i].vid) f = &g_fl[i];
    if (!f) return NULL;
    memset(f, 0, sizeof(*f));
    f->vid = vid; f->af = af; f->slot = slot; f->takeoff = takeoff; f->phase = phase;
    f->t0 = GetTickCount(); f->min_clear = 1e9; f->yaw = 1e9;
    return f;
}
static void fl_end(Flight *f) { lock_free(f->af, f->vid); f->vid = 0; }
static void fl_phase(Flight *f, int ph) {
    EV("vehicle %u: %s -> %s (tile %u)", f->vid, PH_NAME[f->phase], PH_NAME[ph], U32(f->af, A_TILE));
    f->phase = ph;
}
static double fl_secs(Flight *f) { return (GetTickCount() - f->t0) / 1000.0; }

typedef struct { char *body, *actor; double w[3], vel[3], vs; } Air;
static int air_read(char *v, Air *a) {
    a->body = first_body(v);
    a->actor = body_actor(v);
    if (!a->body || !a->actor) return 0;
    float fv[3];
    ((GetVelFn)vt(a->actor, 38 * 4))(a->actor, fv);
    for (int k = 0; k < 3; ++k) { a->w[k] = F64(a->body, 0x48 + 8 * k); a->vel[k] = fv[k]; }
    a->vs = sqrt(a->vel[0] * a->vel[0] + a->vel[1] * a->vel[1] + a->vel[2] * a->vel[2]);
    return 1;
}

static void qmul(const double a[4], const double b[4], double r[4]) {
    r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
/* velocity, no spin, attitude from yaw (nose +z), pitch (nose up > 0) and roll */
static void assist_set(char *actor, const double nv[3], double yaw, double pitch, double roll) {
    static const float zero[3] = {0, 0, 0};
    float fv[3] = {(float)nv[0], (float)nv[1], (float)nv[2]};
    ((SetVelFn)vt(actor, 39 * 4))(actor, fv, 1);
    ((SetVelFn)vt(actor, 41 * 4))(actor, zero, 1);
    double qy[4] = {0, sin(yaw / 2), 0, cos(yaw / 2)};
    double qp[4] = {sin(-pitch / 2), 0, 0, cos(-pitch / 2)};      /* about +x: nose up = negative */
    double qr[4] = {0, 0, sin(roll / 2), cos(roll / 2)};
    double t1[4], q[4];
    qmul(qy, qp, t1);
    qmul(t1, qr, q);
    PxTransform pose;
    ((GetPoseFn)vt(actor, 18 * 4))(actor, &pose);
    for (int i = 0; i < 4; ++i) pose.q[i] = (float)q[i];
    ((SetPoseFn)vt(actor, 19 * 4))(actor, &pose, 1);
}
/* move an aircraft (the carrier elevator way): standing still at w, nose to yaw */
static void put_at(char *v, const double w[3], double yaw) {
    char *actor = body_actor(v), *body = first_body(v);
    if (!actor || !body) return;
    PxTransform pose;
    ((GetPoseFn)vt(actor, 18 * 4))(actor, &pose);
    pose.q[0] = 0.0f; pose.q[1] = (float)sin(yaw / 2); pose.q[2] = 0.0f; pose.q[3] = (float)cos(yaw / 2);
    for (int k = 0; k < 3; ++k) pose.p[k] = (float)(pose.p[k] + (w[k] - F64(body, 0x48 + 8 * k)));
    ((SetPoseFn)vt(actor, 19 * 4))(actor, &pose, 1);
    freeze(actor);
    for (int k = 0; k < 3; ++k) F64(v, V_CTRL_TARGET + 8 * k) = w[k];
}

/* parked in its hangar bay and docked: the island repairs, refuels and rearms it */
static void park(Flight *f, char *v, double stand) {
    double spot[3], yaw;
    bay_world(f->af, f->slot & 3, spot, &yaw);
    Probe pr;
    int hit = 0;
    double floor_y = probe_init(&pr, v) ? probe_down(&pr, spot[0], spot[1] + 5.0, spot[2], &hit) : spot[1];
    if (!hit || fabs(floor_y - spot[1]) > 4.0) floor_y = spot[1];      /* inside the hangar, under its roof */
    EV("vehicle %u: hangar floor %+.2f m (bay %u, stand %.2f m)", f->vid, floor_y - spot[1], f->slot & 3, stand);
    /* set down from a little above the hangar floor: placed into the floor the game destroys it */
    spot[1] = fmax(spot[1], floor_y) + clampd(stand, 0.3, 4.0) + (is_wing(v) ? 1.5 : 1.0);
    put_at(v, spot, yaw);
    ((BrakesFn)vt(v, VT_SET_BRAKES))(v, 1.0f, 1.0f);
    F32(v, V_CTRL_SPEED) = 0.0f;
    F32(v, V_CTRL_FACTOR) = 0.0f;                       /* engine off: parked */
    U8(v, V_CTRL_TAXI) = 1;
    U32(v, V_DOCK_STATE) = DOCK_DOCKED;
    U32(v, V_OP) = 0;
    attach_flags(v, 0);
    STAT(ROLLOUTS);
}

/* the aircraft flies on by itself (its waypoints), like at the end of the vanilla takeoff */
static void hand_back(Flight *f, char *v) {
    U32(v, V_DOCK_STATE) = DOCK_UNDOCKED;
    U32(v, V_DOCK_VEHICLE) = 0;
    U32(v, V_DOCK_TILE) = 0;
    I32(v, V_DOCK_AIRFIELD) = -1;
    as_flying(v);
    U32(v, V_CTRL_MODE) = 0;
    U8(v, V_CTRL_TAXI) = 0;
    F32(v, V_CTRL_FACTOR) = 1.0f;
    if (is_wing(v)) gear(v, 0);
    hangar_release(f->af, f->vid);
}

/* turn rate towards a point, at most max_rate */
static double steer_to(Flight *f, const Air *a, const double t[3], double max_rate) {
    return clampd(wrap(atan2(t[0] - a->w[0], t[2] - a->w[2]) - f->yaw) * 0.8, -max_rate, max_rate);
}

/* fly a plane: turn at `rate`, towards `speed`, climbing / sinking towards height `alt`; vz_path is
 * the vertical speed of the path itself (a glide path sinks), so the plane holds it without lag */
static void plane_fly(Flight *f, const Air *a, double dt, double rate, double speed, double alt,
                      double vz_path, double sink_max, double pitch_add) {
    f->yaw = wrap(f->yaw + rate * dt);
    f->speed += clampd(speed - f->speed, -5.0 * dt, 4.0 * dt);
    double vz = clampd(vz_path + (alt - a->w[1]) * 0.4, -sink_max, 12.0);
    f->vz += clampd(vz - f->vz, -8.0 * dt, 8.0 * dt);
    double hs = sqrt(fmax(f->speed * f->speed - f->vz * f->vz, 100.0));
    double nv[3] = {sin(f->yaw) * hs, f->vz, cos(f->yaw) * hs};
    double roll = P_BANK_SIGN * atan(rate * f->speed / 9.81);
    f->roll += clampd(roll - f->roll, -0.8 * dt, 0.8 * dt);
    assist_set(a->actor, nv, f->yaw, atan2(f->vz, hs) + pitch_add, f->roll);
}

/* never lower than `margin` over the ground under the aircraft, `ahead_s` seconds ahead and at t */
static double ground_floor(Probe *pr, const Air *a, const double t[3], double ahead_s, double margin) {
    double pts[3][2] = {{a->w[0], a->w[2]}, {a->w[0] + a->vel[0] * ahead_s, a->w[2] + a->vel[2] * ahead_s}, {t[0], t[2]}};
    double top = 0.0;
    for (int i = 0; i < 3; ++i) {
        double g = probe_ground(pr, pts[i][0], pts[i][1], NULL);
        if (g > top) top = g;
    }
    return top + margin;
}

static void fl_target(char *v, const double t[3], double alt, float speed) {
    F64(v, V_CTRL_TARGET) = t[0];
    F64(v, V_CTRL_TARGET + 8) = alt;
    F64(v, V_CTRL_TARGET + 16) = t[2];
    F32(v, V_CTRL_SPEED) = speed;
    F32(v, V_CTRL_FACTOR) = 1.0f;
    U32(v, V_CTRL_MODE) = 3;
    U8(v, V_CTRL_TAXI) = 0;
}

/* ---- plane landing ------------------------------------------------------------------------------ */
static void plane_land(Flight *f, char *v, const Air *a, double dt, Probe *pr) {
    Plan *pl = plane_plan(f->af, v);
    Rwy r;
    if (!runway(f->af, pl->rev, &r)) return;
    double s, lat, t[3];
    rwy_frame(&r, a->w[0], a->w[2], &s, &lat);
    double ry = r.thr[1], h = a->w[1] - ry, yl = rwy_yaw(&r);
    double fin = (pl->cruise - pl->h0) / pl->slope + 600.0;   /* length of the straight final */
    double rate = 0.0, speed = P_SPEED, alt = ry + pl->cruise, sink = 15.0, pitch = 0.0, vz_path = 0.0;
    D.dbg[0] = s; D.dbg[1] = lat; D.dbg[4] = a->vs; D.dbg[5] = h;
    rwy_point(&r, s, 0.0, t);

    switch (f->phase) {
    case PH_HOLD: {                                   /* runway busy: orbit beside the pattern */
        if (lock_take(f->af, f->vid)) { fl_phase(f, PH_INBOUND); return; }
        double c[3];
        rwy_point(&r, fin * 0.5, f->side * (P_WIDTH + 1500.0), c);
        double ang = atan2(a->w[0] - c[0], a->w[2] - c[2]) + 0.6;
        t[0] = c[0] + sin(ang) * 700.0; t[2] = c[2] + cos(ang) * 700.0;
        rate = steer_to(f, a, t, P_TURN_RATE);
        break;
    }
    case PH_INBOUND: {
        if (!lock_take(f->af, f->vid)) { f->side = lat >= 0 ? 1 : -1; fl_phase(f, PH_HOLD); return; }
        if (s > 0.6 * fin && fabs(lat) < 0.3 * s && fabs(wrap(yl - f->yaw)) < 0.8) { fl_phase(f, PH_FINAL); return; }
        if (!f->side) f->side = lat >= 0 ? 1 : -1;
        int outbound = fabs(wrap(yl + 3.141592653589793 - f->yaw)) < 0.6;
        if (s > -400.0 && s < fin - 300.0 && fabs(lat - f->side * P_WIDTH) < 350.0 && outbound) { fl_phase(f, PH_DOWNWIND); return; }
        rwy_point(&r, -300.0, f->side * P_WIDTH, t);       /* downwind entry, abeam the runway */
        rate = steer_to(f, a, t, P_TURN_RATE);
        if (hypot(t[0] - a->w[0], t[2] - a->w[2]) < 300.0) fl_phase(f, PH_DOWNWIND);
        break;
    }
    case PH_DOWNWIND:
        rwy_point(&r, fmin(s + 800.0, fin + 600.0), f->side * P_WIDTH, t);
        rate = steer_to(f, a, t, P_TURN_RATE);
        if (s >= fin) fl_phase(f, PH_BASE);
        break;
    case PH_BASE:                                     /* full-rate 180 deg turn towards the axis */
        rate = -f->side * P_TURN_RATE;
        speed = 70.0;
        if (fabs(wrap(yl - f->yaw)) < 0.3) fl_phase(f, PH_FINAL);
        break;
    case PH_FINAL: {
        double glide = clampd(pl->slope * (s + P_AIM), 0.0, pl->cruise);
        int off = (s < -0.85 * r.len) || (s < 300.0 && fabs(lat) > 60.0) || (s < 800.0 && h > glide + 80.0) ||
                  (s < 1500.0 && fabs(wrap(yl - f->yaw)) > 1.2);
        if (off) {                                    /* go round: the pattern again */
            f->go_arounds++;
            EV("vehicle %u: go-around %d (s %.0f m, lateral %.0f m, height %.0f m)", f->vid, f->go_arounds, s, lat, h);
            if (f->go_arounds >= 3) {
                EV("vehicle %u: landing ABORTED after 3 go-arounds (tile %u)", f->vid, U32(f->af, A_TILE));
                hand_back(f, v);
                fl_end(f);
                return;
            }
            lock_free(f->af, f->vid);
            f->side = lat >= 0 ? 1 : -1;
            fl_phase(f, PH_INBOUND);
            return;
        }
        rwy_point(&r, s - fmax(250.0, 2.5 * fabs(lat)), 0.0, t);
        rate = steer_to(f, a, t, P_TURN_RATE);
        alt = ry + glide;
        speed = s < 2000.0 ? P_FINAL_SPEED : P_SPEED;
        sink = fmax(25.0, pl->slope * P_FINAL_SPEED + 6.0);
        if (glide > 0.0 && glide < pl->cruise) vz_path = -pl->slope * f->speed;
        if (s < 2500.0 && !f->gear_set) { gear(v, 1); f->gear_set = 1; }
        if (s < 1000.0 && fabs(lat) > f->max_lat) f->max_lat = fabs(lat);
        if (s < 150.0 && (main_gear_down(v) || h < 2.5 || (s < 0.0 && h < 8.0))) {   /* touchdown (the wire) */
            f->td_speed = a->vs;
            U32(v, V_DOCK_STATE) = DOCK_TAXI;           /* no new orders from here on (like vanilla) */
            as_ground(v);
            gear(v, 1);
            f->speed = sqrt(a->vel[0] * a->vel[0] + a->vel[2] * a->vel[2]);
            fl_phase(f, PH_ROLLOUT);
            return;
        }
        break;
    }
    case PH_ROLLOUT: {                                /* braked like by the carrier's arresting wire */
        double remain = r.len + s;
        double dec = clampd(f->speed * f->speed / (2.0 * fmax(remain - 30.0, 15.0)), 4.0, 20.0);
        f->speed = fmax(f->speed - dec * dt, 0.0);
        f->yaw = wrap(f->yaw + clampd(wrap(yl - f->yaw), -0.3 * dt, 0.3 * dt));
        double side_v = clampd(-lat * 0.5, -3.0, 3.0);
        double vy = main_gear_down(v) ? fmin(a->vel[1], 0.0) : -3.0;   /* still floating: down onto the wheels */
        double nv[3] = {sin(f->yaw) * f->speed - r.az * side_v, vy, cos(f->yaw) * f->speed + r.ax * side_v};
        assist_set(a->actor, nv, f->yaw, 0.0, 0.0);
        fl_target(v, t, ry, 0.0f);
        if (f->speed < 2.0) {
            park(f, v, h);
            EV("vehicle %u: LANDED (plane) tile %u hangar %u in %.0f s, go-arounds %d, final max lateral %.0f m, "
               "min clearance %.0f m, touchdown %.0f m/s", f->vid, U32(f->af, A_TILE), f->slot, fl_secs(f),
               f->go_arounds, f->max_lat, f->min_clear, f->td_speed);
            fl_end(f);
        }
        return;
    }
    }
    if (pr && !(f->phase == PH_FINAL && s < 600.0)) {
        double fl = ground_floor(pr, a, t, 5.0, P_MARGIN);
        if (alt < fl) alt = fl;
        double clear = a->w[1] - probe_ground(pr, a->w[0], a->w[2], NULL);
        if (clear < f->min_clear) f->min_clear = clear;
    }
    plane_fly(f, a, dt, rate, speed, alt, vz_path, sink, pitch);
    fl_target(v, t, alt, 80.0f);
    D.dbg[2] = f->phase; D.dbg[3] = alt; D.dbg[6] = rate;
}

/* ---- plane takeoff ------------------------------------------------------------------------------ */
static void plane_takeoff(Flight *f, char *v, const Air *a, double dt, Probe *pr) {
    Plan *pl = plane_plan(f->af, v);
    Rwy r;
    if (!runway(f->af, pl->trev, &r)) return;
    double s, lat, t[3], ry = r.thr[1], yl = rwy_yaw(&r);
    rwy_frame(&r, a->w[0], a->w[2], &s, &lat);
    double h = a->w[1] - ry;
    D.dbg[0] = s; D.dbg[1] = lat; D.dbg[4] = a->vs; D.dbg[5] = h; D.dbg[2] = f->phase;
    if (f->phase == PH_WAIT) {
        if (!lock_take(f->af, f->vid)) return;
        rwy_point(&r, -25.0, 0.0, t);                 /* lined up just past the threshold */
        t[1] = ry + clampd(f->stand, 0.3, 4.0);
        put_at(v, t, yl);
        U32(v, V_DOCK_STATE) = 3;                     /* undocking */
        as_ground(v);
        gear(v, 1);
        ((BrakesFn)vt(v, VT_SET_BRAKES))(v, 0.0f, 1.0f);
        f->yaw = yl; f->speed = 0.0; f->vz = 0.0; f->roll = 0.0;
        fl_phase(f, PH_ROLL);
        return;
    }
    rwy_point(&r, -(r.len + 3000.0), 0.0, t);         /* straight out along the runway */
    if (f->phase == PH_ROLL) {
        f->speed += 9.0 * dt;
        f->yaw = yl;
        double side_v = clampd(-lat * 0.5, -3.0, 3.0);
        double nv[3] = {sin(yl) * f->speed - r.az * side_v, clampd(a->vel[1], -5.0, 1.0), cos(yl) * f->speed + r.ax * side_v};
        assist_set(a->actor, nv, yl, 0.0, 0.0);
        fl_target(v, t, ry + 200.0, 80.0f);
        if (f->speed >= P_ROTATE_SPEED) { as_flying(v); f->vz = 0.0; fl_phase(f, PH_CLIMB); }
        return;
    }
    double alt = ry + 250.0;
    if (pr) {
        double fl = ground_floor(pr, a, t, 6.0, P_MARGIN);
        if (alt < fl) alt = fl;
        double clear = a->w[1] - probe_ground(pr, a->w[0], a->w[2], NULL);
        if (h > 30.0 && clear < f->min_clear) f->min_clear = clear;
    }
    plane_fly(f, a, dt, steer_to(f, a, t, P_TURN_RATE), P_SPEED, alt, 0.0, 15.0, 0.0);
    fl_target(v, t, alt, 80.0f);
    if (h > 20.0 && f->gear_set == 0) { gear(v, 0); f->gear_set = 1; }
    if (h > P_HANDOFF_HEIGHT && s < -(r.len + 300.0)) {
        hand_back(f, v);
        EV("vehicle %u: TOOK OFF (plane) tile %u in %.0f s, min clearance %.0f m", f->vid, U32(f->af, A_TILE),
           fl_secs(f), f->min_clear);
        fl_end(f);
    }
}

/* ---- helicopters -------------------------------------------------------------------------------- */
static void heli_land(Flight *f, char *v, const Air *a, double dt, Probe *pr) {
    double pad[3];
    pad_world(f->af, f->slot, pad);
    double dx = pad[0] - a->w[0], dz = pad[2] - a->w[2], d = sqrt(dx * dx + dz * dz), h = a->w[1] - pad[1];
    double nv[3], pitch = 0.0, alt = 0.0;
    D.dbg[0] = d; D.dbg[4] = a->vs; D.dbg[5] = h; D.dbg[2] = f->phase;
    if (f->phase == PH_H_INBOUND) {
        /* down an approach line (~14 deg) to a hover 15 m over the pad, braking ~2 m/s^2 to stop over
         * it; above the line it slows down until the descent has caught up */
        alt = clampd(15.0 + 0.25 * d, 15.0, H_CRUISE);
        if (pr && d > 60.0) {
            double fl = ground_floor(pr, a, pad, 4.0, 30.0) - pad[1];
            if (alt < fl) alt = fl;
            double clear = a->w[1] - probe_ground(pr, a->w[0], a->w[2], NULL);
            if (clear < f->min_clear) f->min_clear = clear;
        }
        double want = fmin(H_SPEED, sqrt(2.0 * 2.0 * fmax(d - 1.0, 0.0)));
        double excess = h - alt;
        if (excess > 20.0) want = fmin(want, fmax(6.0, H_SPEED * (1.0 - (excess - 20.0) / 120.0)));
        double ux = d > 0.5 ? dx / d : 0.0, uz = d > 0.5 ? dz / d : 0.0;
        double ex = ux * want - f->hv[0], ez = uz * want - f->hv[1], e = sqrt(ex * ex + ez * ez), lim = 4.0 * dt;
        if (e > lim) { ex *= lim / e; ez *= lim / e; }          /* velocity changes smoothly (<= 4 m/s^2) */
        f->hv[0] += ex; f->hv[1] += ez;
        f->speed = sqrt(f->hv[0] * f->hv[0] + f->hv[1] * f->hv[1]);
        nv[0] = f->hv[0]; nv[2] = f->hv[1];
        nv[1] = clampd((alt - h) * 0.5, -10.0, 5.0);
        if (f->speed > 5.0) f->yaw = wrap(f->yaw + clampd(wrap(atan2(f->hv[0], f->hv[1]) - f->yaw), -0.7 * dt, 0.7 * dt));
        pitch = -0.12 * f->speed / H_SPEED;
        if (d < 3.0 && f->speed < 1.5) { f->t_desc = GetTickCount(); fl_phase(f, PH_H_DESCEND); }
    } else {                                          /* straight down onto the pad */
        double k = d > 0.1 ? fmin(0.5 * d, 2.0) / d : 0.0;
        nv[0] = dx * k; nv[2] = dz * k;
        nv[1] = h > 12.0 ? -4.0 : h > 5.0 ? -2.0 : -1.0;
        /* down: the ground stopped the descent (or the wheels report contact) */
        if (main_gear_down(v) || (h < 3.0 && a->vel[1] > -0.25 && GetTickCount() - f->t_desc > 1500)) {
            park(f, v, h);
            EV("vehicle %u: LANDED (helicopter) tile %u hangar %u in %.0f s, descent %.0f s, min clearance %.0f m",
               f->vid, U32(f->af, A_TILE), f->slot, fl_secs(f), (GetTickCount() - f->t_desc) / 1000.0, f->min_clear);
            fl_end(f);
            return;
        }
    }
    assist_set(a->actor, nv, f->yaw, pitch, 0.0);
    fl_target(v, pad, pad[1] + alt, (float)fmax(f->speed, 10.0));
}

static void heli_takeoff(Flight *f, char *v, const Air *a, double dt) {
    double pad[3];
    pad_world(f->af, f->slot, pad);
    if (!f->lined) {                                  /* out of the hangar onto its pad */
        double p[3] = {pad[0], pad[1] + clampd(f->stand, 0.3, 4.0), pad[2]}, yaw;
        double bay[3];
        bay_world(f->af, f->slot & 3, bay, &yaw);
        put_at(v, p, yaw);
        U32(v, V_DOCK_STATE) = 3;                     /* undocking */
        as_flying(v);
        f->yaw = yaw;
        f->lined = 1;
        return;
    }
    double h = a->w[1] - pad[1];
    double nv[3] = {(pad[0] - a->w[0]) * 0.3, 4.0, (pad[2] - a->w[2]) * 0.3};
    assist_set(a->actor, nv, f->yaw, 0.0, 0.0);
    fl_target(v, pad, pad[1] + H_HANDOFF_HEIGHT + 10.0, 10.0f);
    D.dbg[5] = h; D.dbg[2] = f->phase;
    if (h > H_HANDOFF_HEIGHT) {
        hand_back(f, v);
        EV("vehicle %u: TOOK OFF (helicopter) tile %u in %.0f s", f->vid, U32(f->af, A_TILE), fl_secs(f));
        fl_end(f);
    }
}

/* ---- starting and ending flights ---------------------------------------------------------------- */
static Flight *start_landing(char *af, char *v, int slot) {
    Flight *f = fl_new(U32(v, V_ID), af, (uint32_t)slot, 0, is_wing(v) ? PH_INBOUND : PH_H_INBOUND);
    if (!f) return NULL;
    U32(v, V_DOCK_STATE) = 1;                         /* docking: cancellable by a new order */
    U32(v, V_DOCK_VEHICLE) = 0;
    U32(v, V_DOCK_TILE) = 0;
    I32(v, V_DOCK_AIRFIELD) = -1;
    as_flying(v);
    EV("vehicle %u: landing on tile %u, hangar %d (%s, %.0f m out)", f->vid, U32(af, A_TILE), slot,
       is_wing(v) ? "plane" : "helicopter", af_distance(af, v));
    return f;
}

/* human aircraft in the vanilla landing queue -> our flights once they are near */
static void take_from_queue(char *af, void *srv) {
    Deque *q = (Deque *)(af + A_DOCK_QUEUE);
    for (uint32_t k = 0; q->data && k < q->count;) {
        uint32_t *items = (uint32_t *)(uintptr_t)q->data, id = items[dq_index(q, k)];
        char *v = vehicle_by_id(srv, id);
        int ours = v && is_human(v) && is_air(v) && (!is_wing(v) || (g_planes && g_plane_assist));
        if (ours) F32(v, V_CTRL_FACTOR) = 1.0f;       /* queued aircraft fly with the engine on */
        int slot = ours && af_distance(af, v) < (is_wing(v) ? FD_PLANE_RANGE : FD_HELI_RANGE) ? hangar_slot(af, id) : -1;
        if (slot < 0 || !start_landing(af, v, slot)) { ++k; continue; }
        for (uint32_t j = k; j + 1 < q->count; ++j) items[dq_index(q, j)] = items[dq_index(q, j + 1)];
        q->count--;
    }
}

/* the vanilla code gave the runway to a human aircraft (old saves, or before we took it): ours */
static void take_from_runway(char *af, void *srv) {
    if (U32(af, A_STATE) == 9) { I32(af, A_RUNWAY_VEHICLE) = -1; U32(af, A_STATE) = 0; }   /* saves of v20-v38 */
    if (U32(af, A_STATE) != 3 || I32(af, A_RUNWAY_VEHICLE) == -1) return;
    char *v = vehicle_by_id(srv, U32(af, A_RUNWAY_VEHICLE));
    if (!v || !is_human(v) || (is_wing(v) && !(g_planes && g_plane_assist))) return;
    int slot = hangar_slot(af, U32(v, V_ID));
    if (slot < 0 || !start_landing(af, v, slot)) return;
    I32(af, A_RUNWAY_VEHICLE) = -1;
    U32(af, A_STATE) = 0;
}

/* aircraft that were landing here when the game was saved: nobody flies them after loading */
static void adopt_orphans(char *af, void *srv) {
    Deque *vq = (Deque *)((char *)srv + 0x37c);
    for (uint32_t i = 0; i < vq->count; ++i) {
        char *v = dq_ptr(vq, i);
        if (!v || !is_human(v) || !is_air(v) || U32(v, V_ATTACHED) || U32(v, V_DOCK_VEHICLE)) continue;
        uint32_t st = U32(v, V_DOCK_STATE);
        if ((st != 1 && st != DOCK_TAXI) || fl_find(U32(v, V_ID)) || af_distance(af, v) > FD_PLANE_RANGE) continue;
        if (is_wing(v) && !(g_planes && g_plane_assist)) continue;
        int slot = hangar_slot(af, U32(v, V_ID));
        if (slot < 0) continue;
        Flight *f = start_landing(af, v, slot);
        if (f && st == DOCK_TAXI) {                   /* already down: straight into the hangar */
            park(f, v, 1.0);
            EV("vehicle %u: parked after loading (tile %u)", f->vid, U32(af, A_TILE));
            fl_end(f);
        }
    }
}

static void fly_all(char *af, void *srv) {
    for (int i = 0; i < MAX_FLIGHTS; ++i) {
        Flight *f = &g_fl[i];
        if (!f->vid || f->af != af) continue;
        char *v = vehicle_by_id(srv, f->vid);
        if (!v) {
            EV("vehicle %u: LOST during %s (tile %u)", f->vid, PH_NAME[f->phase], U32(af, A_TILE));
            hangar_release(af, f->vid);
            fl_end(f);
            continue;
        }
        uint32_t st = U32(v, V_DOCK_STATE);
        if (!f->takeoff && st != 1 && st != DOCK_TAXI) {
            EV("vehicle %u: landing ended by the game (dock state %u)", f->vid, st);
            fl_end(f);
            continue;
        }
        Air a;
        if (!air_read(v, &a)) continue;
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        double dt = f->t.QuadPart ? (double)(now.QuadPart - f->t.QuadPart) / (double)freq.QuadPart : 1.0 / 30;
        f->t = now;
        if (dt <= 0.0) continue;
        if (dt > 0.1) dt = 0.1;
        if (f->yaw > 1e8) {                           /* first tick: from the current motion */
            double vh = sqrt(a.vel[0] * a.vel[0] + a.vel[2] * a.vel[2]);
            f->yaw = vh > 5.0 ? atan2(a.vel[0], a.vel[2]) : 0.0;
            f->speed = f->takeoff ? 0.0 : vh;
            f->vz = a.vel[1];
            f->hv[0] = a.vel[0]; f->hv[1] = a.vel[2];
        }
        Probe pr;
        Probe *p = probe_init(&pr, v) ? &pr : NULL;
        if (is_wing(v)) { if (f->takeoff) plane_takeoff(f, v, &a, dt, p); else plane_land(f, v, &a, dt, p); }
        else { if (f->takeoff) heli_takeoff(f, v, &a, dt); else heli_land(f, v, &a, dt, p); }
        STAT(GLIDE_FIXES);
    }
}

/* takeoff of an aircraft parked in an island hangar (H1: it got a new order) */
static void fd_takeoff(void *srv, char *v, char *wp) {
    STAT(TAKEOFF_REQUESTS);
    if (fl_find(U32(v, V_ID))) return;
    char *body = first_body(v);
    char *tile = body ? ((AtPosFn)A->va(F_TILE_AT_POS))((char *)srv + L_TILES, (double *)(body + BODY_POS)) : NULL;
    void *island = tile ? PTR(tile, T_ISLAND) : NULL;
    if (!island || !airfield_count(island)) { STAT(TAKEOFF_REJECTED); return; }
    char *af = find_parked_airfield(island, U32(v, V_ID));
    if (!af) af = airfield_at(island, 0);
    int slot = af ? hangar_slot(af, U32(v, V_ID)) : -1;
    if (slot < 0) { STAT(TAKEOFF_REJECTED); return; }
    if (is_wing(v) && !g_plane_assist) { takeoff(srv, v, wp); return; }   /* pure vanilla */
    Flight *f = fl_new(U32(v, V_ID), af, (uint32_t)slot, 1, is_wing(v) ? PH_WAIT : PH_H_LIFT);
    if (!f) { STAT(TAKEOFF_REJECTED); return; }
    double spot[3], yaw;
    bay_world(af, (uint32_t)slot & 3, spot, &yaw);
    f->stand = F64(body, 0x50) - spot[1];             /* body height over the ground when standing */
    U32(v, V_DOCK_STATE) = DOCK_PENDING_UNDOCK;
    U32(v, V_DOCK_VEHICLE) = 0;
    U32(v, V_DOCK_TILE) = 0;
    I32(v, V_DOCK_AIRFIELD) = -1;
    F32(v, V_CTRL_FACTOR) = 1.0f;
    if (wp && U32(wp, WP_TYPE) == WP_LAND) { U32(wp, WP_TYPE) = WP_MOVE; U32(wp, WP_PARAM) = 0; }
    STAT(TAKEOFF_QUEUED);
    EV("vehicle %u: takeoff from tile %u hangar %d (%s)", f->vid, U32(af, A_TILE), slot, is_wing(v) ? "plane" : "helicopter");
}

/* a new order cancels a landing until touchdown; returns 1 when it did */
static int fd_cancel(char *v) {
    Flight *f = fl_find(U32(v, V_ID));
    if (!f || f->takeoff || f->phase == PH_ROLLOUT) return 0;
    hand_back(f, v);
    STAT(LAND_CANCELLED);
    EV("vehicle %u: landing cancelled by a new order (%s)", f->vid, PH_NAME[f->phase]);
    fl_end(f);
    return 1;
}

static void THISCALL h5_af_tick(void *af_, int a, int b, int c) {
    o_af_tick(af_, a, b, c);
    char *af = af_;
    void *srv = A->server();
    if (!srv) return;
    take_from_runway(af, srv);
    take_from_queue(af, srv);
    if (g_tick % 32 == 5) adopt_orphans(af, srv);
    fly_all(af, srv);
}

/* ---- server tick: pending landings, airfield table for Lua ------------------------------------ */
#define MAX_TABLE 512
static struct { uint32_t tid; int32_t count; } g_table[MAX_TABLE];
static int g_table_n;
static CRITICAL_SECTION g_table_cs;

static void refresh_table(void *srv) {
    Deque *q = (Deque *)((char *)srv + L_TILE_DEQUE);
    if (!q->data || q->count > MAX_TABLE) return;
    EnterCriticalSection(&g_table_cs);
    for (uint32_t i = 0; i < q->count; ++i) {
        char *t = dq_ptr(q, i);
        if (!t) continue;
        uint32_t id = U32(t, T_ID);
        int32_t count = -1;
        void *island = PTR(t, T_ISLAND);
        if (island) count = (int32_t)airfield_count(island);
        else if (U8(t, T_SAVED_SET)) count = I32(t, T_SAVED_COUNT);
        int slot = -1;
        for (int k = 0; k < g_table_n; ++k) if (g_table[k].tid == id) slot = k;
        if (slot < 0 && g_table_n < MAX_TABLE) { slot = g_table_n++; g_table[slot].tid = id; g_table[slot].count = -1; }
        if (slot >= 0 && count >= 0) g_table[slot].count = count;   /* keep what we learned earlier */
    }
    LeaveCriticalSection(&g_table_cs);
    STAT(TABLE_REFRESH);
}

/* developer tool: height map of an airfield in its own coordinates (2 m grid): the top surface (from
 * 60 m up) and the surface under 5 m (inside hangars: floor, walls show up as ~5) */
static void scan_airfield(void *srv, uint32_t tid) {
    char *tile = tile_by_id(srv, tid);
    char *island = tile ? PTR(tile, T_ISLAND) : NULL;
    char *af = island && airfield_count(island) ? airfield_at(island, 0) : NULL;
    Deque *vq = (Deque *)((char *)srv + 0x37c);
    char *probe_v = NULL;                             /* the vehicle nearest to the airfield: its scene */
    double best = 1e18;
    for (uint32_t i = 0; af && i < vq->count; ++i) {
        char *v = dq_ptr(vq, i);
        if (v && body_actor(v) && !U32(v, V_ATTACHED) && af_distance(af, v) < best) { best = af_distance(af, v); probe_v = v; }
    }
    Probe pr;
    if (!af || !probe_v || !probe_init(&pr, probe_v)) { EV("scan of tile %u failed", tid); return; }
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\iaf_scan_%u.txt", A->game_dir, tid);
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    double *m = (double *)(af + A_XFORM);
    fprintf(fp, "# airfield tile %u origin %.2f %.2f %.2f; rows z, cols x; top|low heights over the origin\n", tid, m[9], m[10], m[11]);
    for (double z = -200.0; z <= 100.0; z += 2.0) {
        for (double x = -220.0; x <= 140.0; x += 2.0) {
            double l[3] = {x, 0.0, z}, w[3];
            to_world(af, l, w);
            int h1 = 0, h2 = 0;
            double top = probe_down(&pr, w[0], m[10] + 60.0, w[2], &h1);
            double low = probe_down(&pr, w[0], m[10] + 5.0, w[2], &h2);
            fprintf(fp, "%.1f|%.1f ", h1 ? top - m[10] : -99.0, h2 ? low - m[10] : -99.0);
        }
        fputc('\n', fp);
    }
    fclose(fp);
    EV("scan of tile %u written to %s", tid, path);
}

static void on_tick(void *srv, void *user) {
    ++g_tick;
    if (D.scan_tile) { scan_airfield(srv, (uint32_t)D.scan_tile); D.scan_tile = 0; }
    STAT(SERVER_TICKS);
    D.server = (uint32_t)(uintptr_t)srv;
    if (D.watch_id) D.watch_ptr = (uint32_t)(uintptr_t)vehicle_by_id(srv, D.watch_id);
    if (g_tick % 8 == 0) {
        for (int i = 0; i < MAX_PENDING; ++i) {
            if (!g_pending[i].vid) continue;
            void *v = vehicle_by_id(srv, g_pending[i].vid);
            if (!v || U32(v, V_DOCK_STATE) != DOCK_UNDOCKED) { g_pending[i].vid = 0; continue; }
            char *tile = tile_by_id(srv, g_pending[i].tid);
            if (!tile || U32(tile, T_TEAM) != U32(v, V_TEAM)) { g_pending[i].vid = 0; continue; }
            void *island = PTR(tile, T_ISLAND);
            if (!island) continue;                         /* not loaded yet: keep waiting */
            if (!airfield_count(island)) { g_pending[i].vid = 0; continue; }
            if (!do_land(srv, v, island)) continue;        /* full: retry later */
            g_pending[i].vid = 0;
            STAT(LAND_QUEUED);
            broadcast_waypoints(srv, v);                    /* clients: waypoints cleared */
        }
    }
    if (g_tick % 64 == 1) refresh_table(srv);
}

static void on_session(void *srv, int started, void *user) {
    memset(g_pending, 0, sizeof(g_pending));
    memset(g_fl, 0, sizeof(g_fl));
    memset(g_lock, 0, sizeof(g_lock));
    memset(g_plan, 0, sizeof(g_plan));
    EnterCriticalSection(&g_table_cs);
    g_table_n = 0;
    LeaveCriticalSection(&g_table_cs);
    g_tick = 0;
    D.server = 0;
}

/* ---- Lua: cc2x.island_airfields.* --------------------------------------------------------------- */
/* count(tile_id) -> airfields on the island, or nil when not known yet */
static int lua_count(cc2x_lua *L) {
    int64_t id;
    int32_t n = -1;
    STAT(LUA_QUERIES);
    if (A->lua_tointeger(L, 1, &id)) {
        EnterCriticalSection(&g_table_cs);
        for (int k = 0; k < g_table_n; ++k) if (g_table[k].tid == (uint32_t)id) n = g_table[k].count;
        LeaveCriticalSection(&g_table_cs);
    }
    if (n < 0) A->lua_pushnil(L); else A->lua_pushinteger(L, n);
    return 1;
}

/* planes() -> true when fixed-wing landings are enabled */
static int lua_planes(cc2x_lua *L) { A->lua_pushboolean(L, g_planes); return 1; }

/* land(vehicle_id, tile_id): order a landing directly (no waypoint needed). Runs on the server
 * thread at the next tick; returns true when the request was posted. */
static void land_post(void *srv, void *user) {
    uint32_t vid = (uint32_t)((uintptr_t)user >> 16), tid = (uint32_t)((uintptr_t)user & 0xffff);
    void *v = vehicle_by_id(srv, vid);
    if (!v) return;
    if (land(srv, v, tid) == 1) broadcast_waypoints(srv, v);
}
static int lua_land(cc2x_lua *L) {
    int64_t vid, tid;
    int ok = A->lua_tointeger(L, 1, &vid) && A->lua_tointeger(L, 2, &tid) &&
             vid > 0 && vid < 0x10000 && tid > 0 && tid < 0x10000 &&
             A->server_post(land_post, (void *)(uintptr_t)((vid << 16) | tid));
    A->lua_pushboolean(L, ok);
    return 1;
}

/* test channel used by native/tools: returns cmd, a, b, c, d once */
static int lua_test_poll(cc2x_lua *L) {
    int32_t cmd = InterlockedExchange((volatile LONG *)&D.cmd, 0);
    if (!cmd) return 0;
    A->lua_pushinteger(L, cmd);
    A->lua_pushinteger(L, D.a);
    A->lua_pushinteger(L, D.b);
    A->lua_pushinteger(L, D.c);
    A->lua_pushinteger(L, D.d);
    return 5;
}

/* ---- load ------------------------------------------------------------------------------------- */
CC2X_EXPORT int cc2x_plugin_load(const cc2x_api *api) {
    if (api->api_version < CC2X_API_VERSION || strcmp(api->game_build, "1.5.18-win32")) return 0;
    A = api;
    InitializeCriticalSection(&g_table_cs);
    /* optional settings, kept where mod updates do not overwrite them:
     * %APPDATA%\Carrier Command 2\cc2x\island_airfields.ini   [island_airfields] planes=0 -> helicopters only */
    char ini[MAX_PATH * 2];
    snprintf(ini, sizeof(ini), "%s\\island_airfields.ini", api->config_dir);
    g_planes = GetPrivateProfileIntA("island_airfields", "planes", 1, ini);
    g_plane_assist = GetPrivateProfileIntA("island_airfields", "plane_assist", 1, ini);

    static const uint8_t h1[] = {0xff, 0xb0, 0x50, 0x01, 0x00, 0x00};
    static const uint8_t h4[] = {0x8b, 0x57, 0x6c, 0x33, 0xc0};
    static const uint8_t s1[] = {0x8b, 0x01, 0x8b, 0x40, 0x10, 0xff, 0xd0};
    static const uint8_t s2[] = {0x8b, 0x07, 0x8b, 0x40, 0x10, 0xff, 0xd0};
    uint8_t s3[5] = {0xe8}, h5[5] = {0xe8};
    int32_t rel = (int32_t)(0x0062ca30 - (S3_SITE + 5));
    memcpy(s3 + 1, &rel, 4);
    rel = (int32_t)(0x00628600 - (H5_SITE + 5));
    memcpy(h5 + 1, &rel, 4);

    api->patch_begin(PLUGIN);
    api->patch_mid(H1_SITE, sizeof(h1), h1, h1_waypoints, NULL);
    api->patch_mid(H4_SITE, sizeof(h4), h4, h4_hangar, NULL);
    api->patch_call(S1_SITE, sizeof(s1), s1, (void *)s_is_ai, NULL);
    api->patch_call(S2_SITE, sizeof(s2), s2, (void *)s_is_ai, NULL);
    api->patch_call(S3_SITE, 5, s3, (void *)s_spawner, (void **)&o_spawner);
    api->patch_call(H5_SITE, 5, h5, (void *)h5_af_tick, (void **)&o_af_tick);
    if (!api->patch_commit()) return 0;

    api->on_server_tick(on_tick, NULL);
    api->on_session(on_session, NULL);
    api->lua_add(PLUGIN, "count", lua_count);
    api->lua_add(PLUGIN, "planes", lua_planes);
    api->lua_add(PLUGIN, "land", lua_land);
    api->lua_add(PLUGIN, "test_poll", lua_test_poll);
    api->lua_feature(PLUGIN);
    api->log(PLUGIN, CC2X_LOG_INFO, "Island Airfields v%d active (fixed-wing landings %s)", VERSION,
             !g_planes ? "disabled" : g_plane_assist ? "ENABLED with assist" : "ENABLED, pure vanilla");
    return 1;
}
