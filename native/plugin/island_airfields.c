/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Island Airfields - CC2X plugin (cc2x_island_airfields.dll).
 *
 * Captured islands get working airfields: aircraft land, park in the hangars, are repaired,
 * refuelled and rearmed by the island, and take off again on a new order.
 *
 * Why it is needed: the airfield logic lives in the island controller, which the game only creates
 * for AI teams. The plugin lets the game create it for human islands too (on_island_controller),
 * skips the defence spawns there (on_island_spawn), accepts landing orders (on_waypoints: a
 * waypoint of type barge_load_tile carrying the tile id, or cc2x.island_airfields.land from Lua),
 * launches parked aircraft on a new order, assigns the hangar slot on landing (on_hangar_choice:
 * the game never writes it for human aircraft) and flies every player aircraft that lands on or
 * takes off from an island airfield itself (on_airfield_tick: the flight director - the vanilla
 * island airfield code is unfinished for player aircraft).
 *
 * Uses only the public CC2X API (0.4+, API 3): no game addresses or memory layouts here.
 * Installed as part of the mod: <mod folder>\content\cc2x\cc2x_island_airfields.dll, loaded by CC2X
 * when the mod is enabled in the game's mod menu. No settings are needed; optionally
 * %APPDATA%\Carrier Command 2\cc2x\island_airfields.ini
 *     [island_airfields]
 *     planes=1                ; 0 = helicopters only
 *     plane_assist=1          ; 0 = leave planes to the vanilla airfield code (they crash)
 *
 * Tested (v44; v45 on the world API, save "Test 1"): takeoff + landing of light/heavy planes and
 * helicopters on several islands, two planes sharing one runway, landings resumed after loading a
 * save, cancel by a new order (native/tools/fd_test.py, cancel_test.py).
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cc2x.h"

#define PLUGIN "island_airfields"
#define VERSION 45

static const cc2x_api *A;
static int g_planes;          /* fixed-wing aircraft may land on islands */
static int g_plane_assist;    /* the flight director flies them (0 = pure vanilla) */
static uint32_t g_tick;
static int g_on;              /* all game events subscribed: the plugin is active */

/* fields of the game objects (cc2x.h) */
#define GI(o, f) A->geti(o, f, 0)
#define GF(o, f) A->getf(o, f, 0)
#define SI(o, f, v) A->seti(o, f, 0, v)
#define SF(o, f, v) A->setf(o, f, 0, v)

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
#define EV(...) A->log(PLUGIN, CC2X_LOG_INFO, __VA_ARGS__)

/* ---- helpers --------------------------------------------------------------------------------- */
static uint32_t vid(void *v) { return (uint32_t)GI(v, CC2X_VEHICLE_ID); }
static int dock_state(void *v) { return (int)GI(v, CC2X_VEHICLE_DOCK_STATE); }
static int is_air(void *v) { return GI(v, CC2X_VEHICLE_IS_AIR) != 0; }
static int is_wing(void *v) { return GI(v, CC2X_VEHICLE_IS_WING) != 0; }
static int is_human(void *v) { return !GI(v, CC2X_VEHICLE_TEAM_IS_AI); }
static int can_land(void *v) { return is_air(v) && (g_planes || !is_wing(v)); }

static void *vehicle_by_id(uint32_t id) { return A->find(CC2X_WORLD_VEHICLES, id); }
static void *tile_by_id(uint32_t id) { return A->find(CC2X_WORLD_TILES, id); }
static int airfield_count(void *island) { return A->count(island, CC2X_ISLAND_AIRFIELDS); }
static void *airfield_at(void *island, int i) { return A->ref(island, CC2X_ISLAND_AIRFIELDS, i); }
static uint32_t af_tile(void *af) { return (uint32_t)GI(af, CC2X_AIRFIELD_TILE); }

/* a landing order becomes a plain move order (fly there) */
static void wp_to_move(void *wp) {
    if (wp && GI(wp, CC2X_WAYPOINT_TYPE) == CC2X_WAYPOINT_BARGE_LOAD_TILE) {
        SI(wp, CC2X_WAYPOINT_TYPE, CC2X_WAYPOINT_MOVE);
        SI(wp, CC2X_WAYPOINT_PARAM, 0);
    }
}

/* index of id in an id list of the airfield, -1 if absent */
static int list_find(void *af, int list, uint32_t id) {
    for (int k = 0, n = A->count(af, list); k < n; ++k)
        if ((uint32_t)A->geti(af, list, k) == id) return k;
    return -1;
}

/* ---- pending landings (island not loaded yet) ------------------------------------------------ */
#define MAX_PENDING 32
static struct { uint32_t vid, tid, tick; } g_pending[MAX_PENDING];

static void pending_remove(uint32_t id, int only_old) {
    for (int i = 0; i < MAX_PENDING; ++i)
        if (g_pending[i].vid == id && (!only_old || g_tick - g_pending[i].tick > 30)) g_pending[i].vid = 0;
}
static void pending_add(uint32_t id, uint32_t tid) {
    pending_remove(id, 0);
    for (int i = 0; i < MAX_PENDING; ++i)
        if (!g_pending[i].vid) { g_pending[i].vid = id; g_pending[i].tid = tid; g_pending[i].tick = g_tick; return; }
}

/* ---- hangars --------------------------------------------------------------------------------- */
/* free slots whose vehicle no longer exists or is flying */
static void cleanup_hangars(void *island) {
    for (int i = 0; i < airfield_count(island); ++i) {
        void *af = airfield_at(island, i);
        for (int k = 0, n = A->count(af, CC2X_AIRFIELD_HANGARS); k < n; ++k) {
            uint32_t id = (uint32_t)A->geti(af, CC2X_AIRFIELD_HANGARS, k);
            if (!id) continue;
            void *v = vehicle_by_id(id);
            if (!v || dock_state(v) == CC2X_DOCK_UNDOCKED) { A->seti(af, CC2X_AIRFIELD_HANGARS, k, 0); STAT(HANGAR_CLEANED); }
        }
    }
}
static int hangar_has(void *af, uint32_t id) { return list_find(af, CC2X_AIRFIELD_HANGARS, id) >= 0; }
/* the slot of this vehicle, else a free one (assigned to it), -1 when full */
static int hangar_slot(void *af, uint32_t id) {
    int k = list_find(af, CC2X_AIRFIELD_HANGARS, id);
    if (k >= 0) return k;
    if ((k = list_find(af, CC2X_AIRFIELD_HANGARS, 0)) < 0) return -1;
    A->seti(af, CC2X_AIRFIELD_HANGARS, k, id);
    STAT(HANGAR_ASSIGNED);
    return k;
}
static void hangar_release(void *af, uint32_t id) {
    for (int k = 0, n = A->count(af, CC2X_AIRFIELD_HANGARS); k < n; ++k)
        if ((uint32_t)A->geti(af, CC2X_AIRFIELD_HANGARS, k) == id) A->seti(af, CC2X_AIRFIELD_HANGARS, k, 0);
}
static void *find_parked_airfield(void *island, uint32_t id) {
    for (int i = 0; i < airfield_count(island); ++i) {
        void *af = airfield_at(island, i);
        if (af && hangar_has(af, id)) return af;
    }
    return NULL;
}
static void *island_under(void *v) {
    void *tile = A->tile_at(A->getf(v, CC2X_VEHICLE_POS, 0), A->getf(v, CC2X_VEHICLE_POS, 2));
    void *island = tile ? A->ref(tile, CC2X_TILE_ISLAND, 0) : NULL;
    return island && airfield_count(island) ? island : NULL;
}

/* ---- landing / takeoff ----------------------------------------------------------------------- */
static int do_land(void *v, void *island) {
    cleanup_hangars(island);
    if (!A->request_landing(island, v)) return 0;
    A->list_clear(v, CC2X_VEHICLE_WAYPOINTS);
    return 1;
}

/* 1 queued, 0 rejected, 2 pending (island not loaded) */
static int land(void *v, uint32_t tile_id) {
    STAT(LAND_REQUESTS);
    pending_remove(vid(v), 0);
    if (!can_land(v) || dock_state(v) != CC2X_DOCK_UNDOCKED || GI(v, CC2X_VEHICLE_ATTACHED_TO) ||
        GI(v, CC2X_VEHICLE_DOCK_VEHICLE)) {
        STAT(LAND_REJECTED);
        return 0;
    }
    void *tile = tile_by_id(tile_id);
    if (!tile || GI(tile, CC2X_TILE_TEAM) != GI(v, CC2X_VEHICLE_TEAM)) { STAT(LAND_REJECTED); return 0; }
    void *island = A->ref(tile, CC2X_TILE_ISLAND, 0);
    if (!island) { pending_add(vid(v), tile_id); STAT(LAND_PENDING); return 2; }
    if (!airfield_count(island) || !do_land(v, island)) { STAT(LAND_REJECTED); return 0; }
    STAT(LAND_QUEUED);
    return 1;
}

static void undock_pending(void *v) {
    SI(v, CC2X_VEHICLE_DOCK_STATE, CC2X_DOCK_PENDING_UNDOCK);
    SI(v, CC2X_VEHICLE_DOCK_VEHICLE, 0);
    SI(v, CC2X_VEHICLE_DOCK_TILE, 0);
    SI(v, CC2X_VEHICLE_DOCK_AIRFIELD, -1);
}

/* vanilla takeoff (plane_assist=0): into the airfield's takeoff queue */
static void takeoff(void *v, void *wp) {
    STAT(TAKEOFF_REQUESTS);
    void *island = island_under(v);
    if (!island) { STAT(TAKEOFF_REJECTED); return; }
    void *af = find_parked_airfield(island, vid(v));
    if (!af) {                       /* parked but not registered: use airfield 0 */
        af = airfield_at(island, 0);
        if (!af) { STAT(TAKEOFF_REJECTED); return; }
        hangar_slot(af, vid(v));
    }
    uint32_t id = vid(v);
    if ((uint32_t)GI(af, CC2X_AIRFIELD_RUNWAY_VEHICLE) == id || list_find(af, CC2X_AIRFIELD_DOCK_QUEUE, id) >= 0 ||
        list_find(af, CC2X_AIRFIELD_UNDOCK_QUEUE, id) >= 0 || !A->list_push(af, CC2X_AIRFIELD_UNDOCK_QUEUE, id))
        return;                      /* already queued / on the runway */
    undock_pending(v);
    STAT(TAKEOFF_QUEUED);
    wp_to_move(wp);
}

/* A new order for an aircraft that is queued for / approaching / taxiing on an island airfield
 * cancels that landing, like it does on the carrier. Returns 1 if a landing was cancelled. */
static int fd_cancel(void *v);
static void fd_takeoff(void *v, void *wp);

static int cancel_island_landing(void *v) {
    uint32_t id = vid(v);
    int st = dock_state(v);
    if ((st != CC2X_DOCK_DOCKING && st != CC2X_DOCK_QUEUED && st != CC2X_DOCK_TAXI) || GI(v, CC2X_VEHICLE_DOCK_VEHICLE) ||
        GI(v, CC2X_VEHICLE_ATTACHED_TO))
        return 0;
    for (int t = 0, nt = A->count(NULL, CC2X_WORLD_TILES); t < nt; ++t) {
        void *tile = A->ref(NULL, CC2X_WORLD_TILES, t);
        void *island = tile ? A->ref(tile, CC2X_TILE_ISLAND, 0) : NULL;
        for (int i = 0; island && i < airfield_count(island); ++i) {
            void *af = airfield_at(island, i);
            if (!af) continue;
            int found = 0;
            if ((uint32_t)GI(af, CC2X_AIRFIELD_RUNWAY_VEHICLE) == id) {
                SI(af, CC2X_AIRFIELD_RUNWAY_VEHICLE, -1);
                SI(af, CC2X_AIRFIELD_STATE, 0);
                SI(af, CC2X_AIRFIELD_MODE, 0);
                A->list_clear(af, CC2X_AIRFIELD_TAXI_PATH);
                found = 1;
            }
            int k = list_find(af, CC2X_AIRFIELD_DOCK_QUEUE, id);
            if (k >= 0) { A->list_remove(af, CC2X_AIRFIELD_DOCK_QUEUE, k); found = 1; }
            if (!found) continue;
            hangar_release(af, id);
            SI(v, CC2X_VEHICLE_DOCK_STATE, CC2X_DOCK_UNDOCKED);
            SI(v, CC2X_VEHICLE_FLIGHT_STATE, CC2X_FLIGHT_PARKED);
            SI(v, CC2X_VEHICLE_CONTROL_MODE, 0);
            SI(v, CC2X_VEHICLE_CONTROL_TAXI, 0);
            SI(v, CC2X_VEHICLE_DOCK_AIRFIELD, -1);
            STAT(LAND_CANCELLED);
            EV("vehicle %u: landing cancelled by a new order (tile %u)", id, (uint32_t)GI(tile, CC2X_TILE_ID));
            return 1;
        }
    }
    return 0;
}

/* a player gave an aircraft new waypoints */
static void on_waypoints(void *v, void *user) {
    if (!g_on || !A->server() || !is_air(v) || !is_human(v)) return;
    void *wp = A->ref(v, CC2X_VEHICLE_WAYPOINTS, 0);
    if (!wp) { pending_remove(vid(v), 1); return; }
    if (dock_state(v) == CC2X_DOCK_DOCKED) {
        if (!GI(v, CC2X_VEHICLE_ATTACHED_TO)) fd_takeoff(v, wp);   /* attached = parked on a carrier */
        return;
    }
    if (GI(wp, CC2X_WAYPOINT_TYPE) != CC2X_WAYPOINT_BARGE_LOAD_TILE) {
        pending_remove(vid(v), 1);
        if (!fd_cancel(v)) cancel_island_landing(v);
        return;
    }
    if (land(v, (uint32_t)GI(wp, CC2X_WAYPOINT_PARAM)) != 1) wp_to_move(wp);   /* fly there instead */
}

/* the hangar slot of a landing human aircraft: its own, else a free one */
static int on_hangar_choice(void *af, void *v, void *user) {
    return g_on && v && is_human(v) ? hangar_slot(af, vid(v)) : -1;
}

/* human islands get an island controller too: that is where the airfields live */
static int on_island_controller(int team, void *user) {
    if (!g_on) return 0;
    STAT(ISLANDS_ENABLED);
    return 1;
}

/* ... but no defence spawns there (vanilla never had them on human islands) */
static int on_island_spawn(void *tile, int ai_team, void *user) {
    if (!g_on || ai_team) return 1;
    STAT(SPAWNS_SKIPPED);
    return 0;
}

/* ---- the flight director ----------------------------------------------------------------------
 * The vanilla island airfield code is unfinished for player aircraft: it holds them on a 3.2 km
 * circle until they happen to be at the right angle, flies planes in at 18 m/s down a 22 deg slope
 * (they stall into the ground), only accepts a touchdown sinking slower than 0.5 m/s, and takes
 * off without any heading control (planes leave across the runway). So every human aircraft that
 * lands on or takes off from an island airfield is flown by this plugin from start to end, one
 * explicit phase at a time (every change is logged), with velocity and attitude set on its physics
 * body each tick. The game's autopilot gets the same target, so engines and sound keep running.
 *   plane landing:  inbound -> downwind -> base (180 deg turn) -> final -> rollout -> parked
 *   plane takeoff:  lined up on the runway -> takeoff roll -> climb -> handed back to the game
 *   heli landing:   inbound (braking to a hover over its pad) -> descend -> parked
 *   heli takeoff:   put on its pad -> lift -> handed back to the game
 * Parking and lining up move the aircraft between its hangar bay and the runway / pad, like the
 * carrier's elevator. Only one plane uses the runway at a time; helicopters use their own pads. */

/* any main gear wheel (index >= 1; wheel 0 is the nose wheel) touching the ground */
static int main_gear_down(void *v) {
    for (int i = 1, n = A->count(v, CC2X_VEHICLE_WHEELS); i < n && i < 8; ++i)
        if (A->getf(v, CC2X_VEHICLE_WHEEL_CONTACT, i) > 0.01) return 1;
    return 0;
}

static double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }
static double wrap(double a) { return remainder(a, 6.283185307179586); }

/* ---- terrain: the game's own physics scene. Static surfaces only: terrain and buildings */
/* world height of the first static surface below x,y0,z; *hit = 0: none */
static double probe_down(double x, double y0, double z, int *hit) {
    static const double down[3] = {0.0, -1.0, 0.0};
    double from[3] = {x, y0, z};
    cc2x_hit h;
    int ok = A->raycast(from, down, 4000.0, CC2X_RAY_STATIC, &h);
    if (hit) *hit = ok;
    return ok && h.pos[1] > 0.0 ? h.pos[1] : 0.0;
}
/* world height of the ground (terrain or building) at x,z; *hit = 0 over open sea */
static double probe_ground(double x, double z, int *hit) { return probe_down(x, 3000.0, z, hit); }

/* ---- runway geometry: rev = 0 lands from the start node towards the end node ------------------ */
typedef struct { double thr[3], ax, az, len; } Rwy;

static int runway(void *af, int rev, Rwy *r) {
    double ws[3], we[3];
    for (int k = 0; k < 3; ++k) {
        ws[k] = A->getf(af, CC2X_AIRFIELD_RUNWAY_START, k);
        we[k] = A->getf(af, CC2X_AIRFIELD_RUNWAY_END, k);
    }
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
static double rwy_yaw(const Rwy *r) { return cc2x_yaw_of(r->ax, r->az); }

/* ---- approach plan, once per airfield: the terrain around it is scanned. The pattern height clears
 * everything around; of the two runway ends and a few glide slopes the flattest final that clears
 * the ground on the way in is taken, and the takeoff direction with the clearer climb-out. */
#define P_MARGIN 40.0               /* clearance over terrain and buildings */
#define P_CRUISE_MIN 250.0          /* the vanilla holding height */
#define P_SCAN_RADIUS 3500.0
#define P_AIM 60.0                  /* the glide path meets the runway this far past the threshold */
#define P_SLOPE_MIN 0.12            /* ~7 deg */
#define P_SLOPE_MAX 0.40            /* ~22 deg: 25 m/s sink at the final speed */
typedef struct { void *af; int rev, trev; double slope, h0, cruise; } Plan;
static Plan g_plan[16];
static unsigned g_plan_next;

static Plan *plane_plan(void *af) {
    for (int i = 0; i < 16; ++i) if (g_plan[i].af == af) return &g_plan[i];
    static Plan fallback;
    Plan *pl = &fallback;
    pl->af = NULL; pl->rev = 0; pl->trev = 0; pl->slope = 0.25; pl->h0 = 20.0; pl->cruise = P_CRUISE_MIN;
    Rwy r[2];
    if (!runway(af, 0, &r[0]) || !runway(af, 1, &r[1])) return pl;
    int hit = 0;
    double ry = r[0].thr[1];
    double g = probe_ground(r[0].thr[0], r[0].thr[2], &hit);
    if (!hit || fabs(g - ry) > 15.0) return pl;           /* island not in the physics world (yet): retry */

    double ox = A->getf(af, CC2X_AIRFIELD_TRANSFORM, 9), oz = A->getf(af, CC2X_AIRFIELD_TRANSFORM, 11), top = 0.0;
    for (double dx = -P_SCAN_RADIUS; dx <= P_SCAN_RADIUS; dx += 125.0)
        for (double dz = -P_SCAN_RADIUS; dz <= P_SCAN_RADIUS; dz += 125.0) {
            if (dx * dx + dz * dz > P_SCAN_RADIUS * P_SCAN_RADIUS) continue;
            double gr = probe_ground(ox + dx, oz + dz, NULL) - ry;
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
                double gr = probe_ground(p[0], p[2], NULL) - ry + fmin(P_MARGIN, 8.0 + 0.15 * d);
                if (gr / (d + P_AIM) > req[rev]) req[rev] = gr / (d + P_AIM);
                rwy_point(&r[rev], -r[rev].len - d, lat, p);
                gr = probe_ground(p[0], p[2], NULL) - ry + P_MARGIN - 0.2 * (d + r[rev].len * 0.5);
                if (gr > out[rev]) out[rev] = gr;
            }
    }
    int best_rev = req[1] < req[0] ? 1 : 0;
    double slope = clampd(req[best_rev], P_SLOPE_MIN, P_SLOPE_MAX);
    if (req[best_rev] > P_SLOPE_MAX)
        EV("tile %u: the terrain needs a %.2f glide slope, more than %.2f", af_tile(af), req[best_rev], P_SLOPE_MAX);

    pl = &g_plan[g_plan_next++ % 16];
    pl->af = af;
    pl->rev = best_rev;
    pl->trev = out[1] < out[0] ? 1 : 0;
    pl->slope = slope;
    pl->h0 = slope * P_AIM;
    pl->cruise = top + 100.0 > P_CRUISE_MIN ? top + 100.0 : P_CRUISE_MIN;
    EV("tile %u plan: land from the %s end, slope %.2f, %.0f m over the threshold; take off from the %s end; "
       "pattern %.0f m (highest ground %.0f m)", af_tile(af), best_rev ? "far" : "vanilla", pl->slope, pl->h0,
       pl->trev ? "far" : "vanilla", pl->cruise, top);
    return pl;
}

/* helicopter pads on the open taxiway between the hangars (z -20..20) and the runway (z -140) */
static void pad_world(void *af, uint32_t slot, double w[3]) {
    double l[3] = {(slot & 1) ? 12.0 : -12.0, 0.0, -70.0 - ((slot & 2) ? 25.0 : 0.0)};
    A->airfield_to_world(af, l, w);
}

static double af_distance(void *af, void *v) {
    double dx = A->getf(v, CC2X_VEHICLE_POS, 0) - A->getf(af, CC2X_AIRFIELD_TRANSFORM, 9);
    double dz = A->getf(v, CC2X_VEHICLE_POS, 2) - A->getf(af, CC2X_AIRFIELD_TRANSFORM, 11);
    return sqrt(dx * dx + dz * dz);
}

/* ---- vehicle flags as the vanilla airfield code sets them --------------------------------------- */
static void attach_flags(void *v, int flying) {
    for (int i = 0, n = A->count(v, CC2X_VEHICLE_ATTACHMENTS); i < n; ++i) A->seti(v, CC2X_VEHICLE_ATTACHMENT_FLYING, i, flying);
}
static void as_flying(void *v) { SI(v, CC2X_VEHICLE_FLIGHT_STATE, CC2X_FLIGHT_FLYING); attach_flags(v, 1); }
static void as_ground(void *v) { SI(v, CC2X_VEHICLE_FLIGHT_STATE, CC2X_FLIGHT_GROUND); attach_flags(v, 0); }

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
    void *af;
    int phase, takeoff, side, lined, gear_set, go_arounds;
    double yaw, speed, vz, roll, stand, hv[2];
    uint32_t t_desc;
    double max_lat, min_clear, td_speed;
    LARGE_INTEGER t;
} Flight;
#define MAX_FLIGHTS 32
static Flight g_fl[MAX_FLIGHTS];
static struct { void *af; uint32_t vid; } g_lock[16];   /* one plane on a runway at a time */

static int lock_take(void *af, uint32_t id) {
    int free_i = -1;
    for (int i = 0; i < 16; ++i) {
        if (g_lock[i].af == af) return g_lock[i].vid == id;
        if (!g_lock[i].af && free_i < 0) free_i = i;
    }
    if (free_i < 0) return 0;
    g_lock[free_i].af = af; g_lock[free_i].vid = id;
    return 1;
}
static void lock_free(void *af, uint32_t id) {
    for (int i = 0; i < 16; ++i) if (g_lock[i].af == af && g_lock[i].vid == id) g_lock[i].af = NULL;
}

static Flight *fl_find(uint32_t id) {
    for (int i = 0; i < MAX_FLIGHTS; ++i) if (g_fl[i].vid == id) return &g_fl[i];
    return NULL;
}
static Flight *fl_new(uint32_t id, void *af, uint32_t slot, int takeoff, int phase) {
    Flight *f = fl_find(id);
    for (int i = 0; i < MAX_FLIGHTS && !f; ++i) if (!g_fl[i].vid) f = &g_fl[i];
    if (!f) return NULL;
    memset(f, 0, sizeof(*f));
    f->vid = id; f->af = af; f->slot = slot; f->takeoff = takeoff; f->phase = phase;
    f->t0 = GetTickCount(); f->min_clear = 1e9; f->yaw = 1e9;
    return f;
}
static void fl_end(Flight *f) { lock_free(f->af, f->vid); f->vid = 0; }
static void fl_phase(Flight *f, int ph) {
    EV("vehicle %u: %s -> %s (tile %u)", f->vid, PH_NAME[f->phase], PH_NAME[ph], af_tile(f->af));
    f->phase = ph;
}
static double fl_secs(Flight *f) { return (GetTickCount() - f->t0) / 1000.0; }

typedef struct { double w[3], vel[3], vs; } Air;
static int air_read(void *v, Air *a) {
    cc2x_body b;
    if (!A->body_get(v, &b)) return 0;
    for (int k = 0; k < 3; ++k) { a->w[k] = b.pos[k]; a->vel[k] = b.vel[k]; }
    a->vs = sqrt(a->vel[0] * a->vel[0] + a->vel[1] * a->vel[1] + a->vel[2] * a->vel[2]);
    return 1;
}

/* velocity, no spin, attitude from yaw (nose +z), pitch (nose up > 0) and roll */
static void assist_set(void *v, const double nv[3], double yaw, double pitch, double roll) {
    cc2x_body b;
    memset(&b, 0, sizeof(b));
    for (int k = 0; k < 3; ++k) b.vel[k] = nv[k];
    cc2x_quat_from_ypr(yaw, pitch, roll, b.rot);
    A->body_set(v, &b, CC2X_BODY_VEL | CC2X_BODY_SPIN | CC2X_BODY_ROT);
}
/* move an aircraft (the carrier elevator way): standing still at w, nose to yaw */
static void put_at(void *v, const double w[3], double yaw) {
    cc2x_body b;
    memset(&b, 0, sizeof(b));
    for (int k = 0; k < 3; ++k) b.pos[k] = w[k];
    cc2x_quat_from_ypr(yaw, 0.0, 0.0, b.rot);
    if (!A->body_set(v, &b, CC2X_BODY_ALL)) return;
    for (int k = 0; k < 3; ++k) A->setf(v, CC2X_VEHICLE_CONTROL_TARGET, k, w[k]);
}

/* parked in its hangar bay and docked: the island repairs, refuels and rearms it */
static void park(Flight *f, void *v, double stand) {
    double spot[3], yaw;
    A->airfield_bay(f->af, (int)(f->slot & 3), spot, &yaw);
    int hit = 0;
    double floor_y = probe_down(spot[0], spot[1] + 5.0, spot[2], &hit);
    if (!hit || fabs(floor_y - spot[1]) > 4.0) floor_y = spot[1];      /* inside the hangar, under its roof */
    EV("vehicle %u: hangar floor %+.2f m (bay %u, stand %.2f m)", f->vid, floor_y - spot[1], f->slot & 3, stand);
    /* set down from a little above the hangar floor: placed into the floor the game destroys it */
    spot[1] = fmax(spot[1], floor_y) + clampd(stand, 0.3, 4.0) + (is_wing(v) ? 1.5 : 1.0);
    put_at(v, spot, yaw);
    A->vehicle_brakes(v, 1.0f, 1.0f);
    SF(v, CC2X_VEHICLE_CONTROL_SPEED, 0.0);
    SF(v, CC2X_VEHICLE_CONTROL_POWER, 0.0);             /* engine off: parked */
    SI(v, CC2X_VEHICLE_CONTROL_TAXI, 1);
    SI(v, CC2X_VEHICLE_DOCK_STATE, CC2X_DOCK_DOCKED);
    SI(v, CC2X_VEHICLE_FLIGHT_STATE, CC2X_FLIGHT_PARKED);
    attach_flags(v, 0);
    STAT(ROLLOUTS);
}

/* the aircraft flies on by itself (its waypoints), like at the end of the vanilla takeoff */
static void hand_back(Flight *f, void *v) {
    SI(v, CC2X_VEHICLE_DOCK_STATE, CC2X_DOCK_UNDOCKED);
    SI(v, CC2X_VEHICLE_DOCK_VEHICLE, 0);
    SI(v, CC2X_VEHICLE_DOCK_TILE, 0);
    SI(v, CC2X_VEHICLE_DOCK_AIRFIELD, -1);
    as_flying(v);
    SI(v, CC2X_VEHICLE_CONTROL_MODE, 0);
    SI(v, CC2X_VEHICLE_CONTROL_TAXI, 0);
    SF(v, CC2X_VEHICLE_CONTROL_POWER, 1.0);
    if (is_wing(v)) A->vehicle_gear(v, 0);
    hangar_release(f->af, f->vid);
}

/* turn rate towards a point, at most max_rate */
static double steer_to(Flight *f, const Air *a, const double t[3], double max_rate) {
    return clampd(wrap(atan2(t[0] - a->w[0], t[2] - a->w[2]) - f->yaw) * 0.8, -max_rate, max_rate);
}

/* fly a plane: turn at `rate`, towards `speed`, climbing / sinking towards height `alt`; vz_path is
 * the vertical speed of the path itself (a glide path sinks), so the plane holds it without lag */
static void plane_fly(Flight *f, void *v, const Air *a, double dt, double rate, double speed, double alt,
                      double vz_path, double sink_max, double pitch_add) {
    f->yaw = wrap(f->yaw + rate * dt);
    f->speed += clampd(speed - f->speed, -5.0 * dt, 4.0 * dt);
    double vz = clampd(vz_path + (alt - a->w[1]) * 0.4, -sink_max, 12.0);
    f->vz += clampd(vz - f->vz, -8.0 * dt, 8.0 * dt);
    double hs = sqrt(fmax(f->speed * f->speed - f->vz * f->vz, 100.0));
    double nv[3] = {sin(f->yaw) * hs, f->vz, cos(f->yaw) * hs};
    double roll = P_BANK_SIGN * atan(rate * f->speed / 9.81);
    f->roll += clampd(roll - f->roll, -0.8 * dt, 0.8 * dt);
    assist_set(v, nv, f->yaw, atan2(f->vz, hs) + pitch_add, f->roll);
}

/* never lower than `margin` over the ground under the aircraft, `ahead_s` seconds ahead and at t */
static double ground_floor(const Air *a, const double t[3], double ahead_s, double margin) {
    double pts[3][2] = {{a->w[0], a->w[2]}, {a->w[0] + a->vel[0] * ahead_s, a->w[2] + a->vel[2] * ahead_s}, {t[0], t[2]}};
    double top = 0.0;
    for (int i = 0; i < 3; ++i) {
        double g = probe_ground(pts[i][0], pts[i][1], NULL);
        if (g > top) top = g;
    }
    return top + margin;
}

static void fl_target(void *v, const double t[3], double alt, double speed) {
    A->setf(v, CC2X_VEHICLE_CONTROL_TARGET, 0, t[0]);
    A->setf(v, CC2X_VEHICLE_CONTROL_TARGET, 1, alt);
    A->setf(v, CC2X_VEHICLE_CONTROL_TARGET, 2, t[2]);
    SF(v, CC2X_VEHICLE_CONTROL_SPEED, speed);
    SF(v, CC2X_VEHICLE_CONTROL_POWER, 1.0);
    SI(v, CC2X_VEHICLE_CONTROL_MODE, 3);
    SI(v, CC2X_VEHICLE_CONTROL_TAXI, 0);
}

/* ---- plane landing ------------------------------------------------------------------------------ */
static void plane_land(Flight *f, void *v, const Air *a, double dt) {
    Plan *pl = plane_plan(f->af);
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
                EV("vehicle %u: landing ABORTED after 3 go-arounds (tile %u)", f->vid, af_tile(f->af));
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
        if (s < 2500.0 && !f->gear_set) { A->vehicle_gear(v, 1); f->gear_set = 1; }
        if (s < 1000.0 && fabs(lat) > f->max_lat) f->max_lat = fabs(lat);
        if (s < 150.0 && (main_gear_down(v) || h < 2.5 || (s < 0.0 && h < 8.0))) {   /* touchdown (the wire) */
            f->td_speed = a->vs;
            SI(v, CC2X_VEHICLE_DOCK_STATE, CC2X_DOCK_TAXI);   /* no new orders from here on (like vanilla) */
            as_ground(v);
            A->vehicle_gear(v, 1);
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
        assist_set(v, nv, f->yaw, 0.0, 0.0);
        fl_target(v, t, ry, 0.0);
        if (f->speed < 2.0) {
            park(f, v, h);
            EV("vehicle %u: LANDED (plane) tile %u hangar %u in %.0f s, go-arounds %d, final max lateral %.0f m, "
               "min clearance %.0f m, touchdown %.0f m/s", f->vid, af_tile(f->af), f->slot, fl_secs(f),
               f->go_arounds, f->max_lat, f->min_clear, f->td_speed);
            fl_end(f);
        }
        return;
    }
    }
    if (!(f->phase == PH_FINAL && s < 600.0)) {
        double fl = ground_floor(a, t, 5.0, P_MARGIN);
        if (alt < fl) alt = fl;
        double clear = a->w[1] - probe_ground(a->w[0], a->w[2], NULL);
        if (clear < f->min_clear) f->min_clear = clear;
    }
    plane_fly(f, v, a, dt, rate, speed, alt, vz_path, sink, pitch);
    fl_target(v, t, alt, 80.0);
    D.dbg[2] = f->phase; D.dbg[3] = alt; D.dbg[6] = rate;
}

/* ---- plane takeoff ------------------------------------------------------------------------------ */
static void plane_takeoff(Flight *f, void *v, const Air *a, double dt) {
    Plan *pl = plane_plan(f->af);
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
        SI(v, CC2X_VEHICLE_DOCK_STATE, CC2X_DOCK_UNDOCKING);
        as_ground(v);
        A->vehicle_gear(v, 1);
        A->vehicle_brakes(v, 0.0f, 1.0f);
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
        assist_set(v, nv, yl, 0.0, 0.0);
        fl_target(v, t, ry + 200.0, 80.0);
        if (f->speed >= P_ROTATE_SPEED) { as_flying(v); f->vz = 0.0; fl_phase(f, PH_CLIMB); }
        return;
    }
    double alt = ry + 250.0;
    double fl = ground_floor(a, t, 6.0, P_MARGIN);
    if (alt < fl) alt = fl;
    double clear = a->w[1] - probe_ground(a->w[0], a->w[2], NULL);
    if (h > 30.0 && clear < f->min_clear) f->min_clear = clear;
    plane_fly(f, v, a, dt, steer_to(f, a, t, P_TURN_RATE), P_SPEED, alt, 0.0, 15.0, 0.0);
    fl_target(v, t, alt, 80.0);
    if (h > 20.0 && f->gear_set == 0) { A->vehicle_gear(v, 0); f->gear_set = 1; }
    if (h > P_HANDOFF_HEIGHT && s < -(r.len + 300.0)) {
        hand_back(f, v);
        EV("vehicle %u: TOOK OFF (plane) tile %u in %.0f s, min clearance %.0f m", f->vid, af_tile(f->af),
           fl_secs(f), f->min_clear);
        fl_end(f);
    }
}

/* ---- helicopters -------------------------------------------------------------------------------- */
static void heli_land(Flight *f, void *v, const Air *a, double dt) {
    double pad[3];
    pad_world(f->af, f->slot, pad);
    double dx = pad[0] - a->w[0], dz = pad[2] - a->w[2], d = sqrt(dx * dx + dz * dz), h = a->w[1] - pad[1];
    double nv[3], pitch = 0.0, alt = 0.0;
    D.dbg[0] = d; D.dbg[4] = a->vs; D.dbg[5] = h; D.dbg[2] = f->phase;
    if (f->phase == PH_H_INBOUND) {
        /* down an approach line (~14 deg) to a hover 15 m over the pad, braking ~2 m/s^2 to stop over
         * it; above the line it slows down until the descent has caught up */
        alt = clampd(15.0 + 0.25 * d, 15.0, H_CRUISE);
        if (d > 60.0) {
            double fl = ground_floor(a, pad, 4.0, 30.0) - pad[1];
            if (alt < fl) alt = fl;
            double clear = a->w[1] - probe_ground(a->w[0], a->w[2], NULL);
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
               f->vid, af_tile(f->af), f->slot, fl_secs(f), (GetTickCount() - f->t_desc) / 1000.0, f->min_clear);
            fl_end(f);
            return;
        }
    }
    assist_set(v, nv, f->yaw, pitch, 0.0);
    fl_target(v, pad, pad[1] + alt, fmax(f->speed, 10.0));
}

static void heli_takeoff(Flight *f, void *v, const Air *a, double dt) {
    double pad[3];
    pad_world(f->af, f->slot, pad);
    if (!f->lined) {                                  /* out of the hangar onto its pad */
        double p[3] = {pad[0], pad[1] + clampd(f->stand, 0.3, 4.0), pad[2]}, yaw;
        A->airfield_bay(f->af, (int)(f->slot & 3), NULL, &yaw);
        put_at(v, p, yaw);
        SI(v, CC2X_VEHICLE_DOCK_STATE, CC2X_DOCK_UNDOCKING);
        as_flying(v);
        f->yaw = yaw;
        f->lined = 1;
        return;
    }
    double h = a->w[1] - pad[1];
    double nv[3] = {(pad[0] - a->w[0]) * 0.3, 4.0, (pad[2] - a->w[2]) * 0.3};
    assist_set(v, nv, f->yaw, 0.0, 0.0);
    fl_target(v, pad, pad[1] + H_HANDOFF_HEIGHT + 10.0, 10.0);
    D.dbg[5] = h; D.dbg[2] = f->phase;
    if (h > H_HANDOFF_HEIGHT) {
        hand_back(f, v);
        EV("vehicle %u: TOOK OFF (helicopter) tile %u in %.0f s", f->vid, af_tile(f->af), fl_secs(f));
        fl_end(f);
    }
}

/* ---- starting and ending flights ---------------------------------------------------------------- */
static Flight *start_landing(void *af, void *v, int slot) {
    Flight *f = fl_new(vid(v), af, (uint32_t)slot, 0, is_wing(v) ? PH_INBOUND : PH_H_INBOUND);
    if (!f) return NULL;
    SI(v, CC2X_VEHICLE_DOCK_STATE, CC2X_DOCK_DOCKING);  /* cancellable by a new order */
    SI(v, CC2X_VEHICLE_DOCK_VEHICLE, 0);
    SI(v, CC2X_VEHICLE_DOCK_TILE, 0);
    SI(v, CC2X_VEHICLE_DOCK_AIRFIELD, -1);
    as_flying(v);
    EV("vehicle %u: landing on tile %u, hangar %d (%s, %.0f m out)", f->vid, af_tile(af), slot,
       is_wing(v) ? "plane" : "helicopter", af_distance(af, v));
    return f;
}

static int ours(void *v) { return v && is_human(v) && is_air(v) && (!is_wing(v) || (g_planes && g_plane_assist)); }

/* human aircraft in the vanilla landing queue -> our flights once they are near */
static void take_from_queue(void *af) {
    for (int k = 0; k < A->count(af, CC2X_AIRFIELD_DOCK_QUEUE);) {
        uint32_t id = (uint32_t)A->geti(af, CC2X_AIRFIELD_DOCK_QUEUE, k);
        void *v = vehicle_by_id(id);
        int mine = ours(v);
        if (mine) SF(v, CC2X_VEHICLE_CONTROL_POWER, 1.0);   /* queued aircraft fly with the engine on */
        int slot = mine && af_distance(af, v) < (is_wing(v) ? FD_PLANE_RANGE : FD_HELI_RANGE) ? hangar_slot(af, id) : -1;
        if (slot < 0 || !start_landing(af, v, slot)) { ++k; continue; }
        A->list_remove(af, CC2X_AIRFIELD_DOCK_QUEUE, k);
    }
}

/* the vanilla code gave the runway to a human aircraft (old saves, or before we took it): ours */
static void take_from_runway(void *af) {
    if (GI(af, CC2X_AIRFIELD_STATE) == 9) { SI(af, CC2X_AIRFIELD_RUNWAY_VEHICLE, -1); SI(af, CC2X_AIRFIELD_STATE, 0); }   /* saves of v20-v38 */
    int64_t rv = GI(af, CC2X_AIRFIELD_RUNWAY_VEHICLE);
    if (GI(af, CC2X_AIRFIELD_STATE) != 3 || rv == -1) return;
    void *v = vehicle_by_id((uint32_t)rv);
    if (!v || !is_human(v) || (is_wing(v) && !(g_planes && g_plane_assist))) return;
    int slot = hangar_slot(af, vid(v));
    if (slot < 0 || !start_landing(af, v, slot)) return;
    SI(af, CC2X_AIRFIELD_RUNWAY_VEHICLE, -1);
    SI(af, CC2X_AIRFIELD_STATE, 0);
}

/* aircraft that were landing here when the game was saved: nobody flies them after loading */
static void adopt_orphans(void *af) {
    for (int i = 0, n = A->count(NULL, CC2X_WORLD_VEHICLES); i < n; ++i) {
        void *v = A->ref(NULL, CC2X_WORLD_VEHICLES, i);
        if (!v || !is_human(v) || !is_air(v) || GI(v, CC2X_VEHICLE_ATTACHED_TO) || GI(v, CC2X_VEHICLE_DOCK_VEHICLE)) continue;
        int st = dock_state(v);
        if ((st != CC2X_DOCK_DOCKING && st != CC2X_DOCK_TAXI) || fl_find(vid(v)) || af_distance(af, v) > FD_PLANE_RANGE) continue;
        if (is_wing(v) && !(g_planes && g_plane_assist)) continue;
        int slot = hangar_slot(af, vid(v));
        if (slot < 0) continue;
        Flight *f = start_landing(af, v, slot);
        if (f && st == CC2X_DOCK_TAXI) {              /* already down: straight into the hangar */
            park(f, v, 1.0);
            EV("vehicle %u: parked after loading (tile %u)", f->vid, af_tile(af));
            fl_end(f);
        }
    }
}

static void fly_all(void *af) {
    for (int i = 0; i < MAX_FLIGHTS; ++i) {
        Flight *f = &g_fl[i];
        if (!f->vid || f->af != af) continue;
        void *v = vehicle_by_id(f->vid);
        if (!v) {
            EV("vehicle %u: LOST during %s (tile %u)", f->vid, PH_NAME[f->phase], af_tile(af));
            hangar_release(af, f->vid);
            fl_end(f);
            continue;
        }
        int st = dock_state(v);
        if (!f->takeoff && st != CC2X_DOCK_DOCKING && st != CC2X_DOCK_TAXI) {
            EV("vehicle %u: landing ended by the game (dock state %d)", f->vid, st);
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
        if (is_wing(v)) { if (f->takeoff) plane_takeoff(f, v, &a, dt); else plane_land(f, v, &a, dt); }
        else { if (f->takeoff) heli_takeoff(f, v, &a, dt); else heli_land(f, v, &a, dt); }
        STAT(GLIDE_FIXES);
    }
}

/* takeoff of an aircraft parked in an island hangar (it got a new order) */
static void fd_takeoff(void *v, void *wp) {
    STAT(TAKEOFF_REQUESTS);
    if (fl_find(vid(v))) return;
    void *island = island_under(v);
    if (!island) { STAT(TAKEOFF_REJECTED); return; }
    void *af = find_parked_airfield(island, vid(v));
    if (!af) af = airfield_at(island, 0);
    int slot = af ? hangar_slot(af, vid(v)) : -1;
    if (slot < 0) { STAT(TAKEOFF_REJECTED); return; }
    if (is_wing(v) && !g_plane_assist) { takeoff(v, wp); return; }   /* pure vanilla */
    Flight *f = fl_new(vid(v), af, (uint32_t)slot, 1, is_wing(v) ? PH_WAIT : PH_H_LIFT);
    if (!f) { STAT(TAKEOFF_REJECTED); return; }
    double spot[3], yaw;
    A->airfield_bay(af, slot & 3, spot, &yaw);
    f->stand = A->getf(v, CC2X_VEHICLE_POS, 1) - spot[1];   /* body height over the ground when standing */
    undock_pending(v);
    SF(v, CC2X_VEHICLE_CONTROL_POWER, 1.0);
    wp_to_move(wp);
    STAT(TAKEOFF_QUEUED);
    EV("vehicle %u: takeoff from tile %u hangar %d (%s)", f->vid, af_tile(af), slot, is_wing(v) ? "plane" : "helicopter");
}

/* a new order cancels a landing until touchdown; returns 1 when it did */
static int fd_cancel(void *v) {
    Flight *f = fl_find(vid(v));
    if (!f || f->takeoff || f->phase == PH_ROLLOUT) return 0;
    hand_back(f, v);
    STAT(LAND_CANCELLED);
    EV("vehicle %u: landing cancelled by a new order (%s)", f->vid, PH_NAME[f->phase]);
    fl_end(f);
    return 1;
}

static void on_airfield_tick(void *af, void *user) {
    if (!g_on || !A->server()) return;
    take_from_runway(af);
    take_from_queue(af);
    if (g_tick % 32 == 5) adopt_orphans(af);
    fly_all(af);
}

/* ---- server tick: pending landings, airfield table for Lua ------------------------------------ */
#define MAX_TABLE 512
static struct { uint32_t tid; int32_t count; } g_table[MAX_TABLE];
static int g_table_n;
static CRITICAL_SECTION g_table_cs;

static void refresh_table(void) {
    int n = A->count(NULL, CC2X_WORLD_TILES);
    if (n > MAX_TABLE) return;
    EnterCriticalSection(&g_table_cs);
    for (int i = 0; i < n; ++i) {
        void *t = A->ref(NULL, CC2X_WORLD_TILES, i);
        if (!t) continue;
        uint32_t id = (uint32_t)GI(t, CC2X_TILE_ID);
        int32_t count = (int32_t)GI(t, CC2X_TILE_AIRFIELDS);
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
static void scan_airfield(uint32_t tid) {
    void *tile = tile_by_id(tid);
    void *island = tile ? A->ref(tile, CC2X_TILE_ISLAND, 0) : NULL;
    void *af = island && airfield_count(island) ? airfield_at(island, 0) : NULL;
    char path[MAX_PATH];
    FILE *fp;
    snprintf(path, sizeof(path), "%s\\iaf_scan_%u.txt", A->game_dir, tid);
    if (!af || !(fp = fopen(path, "w"))) { EV("scan of tile %u failed", tid); return; }
    double o[3], zero[3] = {0, 0, 0};
    A->airfield_to_world(af, zero, o);
    fprintf(fp, "# airfield tile %u origin %.2f %.2f %.2f; rows z, cols x; top|low heights over the origin\n", tid, o[0], o[1], o[2]);
    for (double z = -200.0; z <= 100.0; z += 2.0) {
        for (double x = -220.0; x <= 140.0; x += 2.0) {
            double l[3] = {x, 0.0, z}, w[3];
            A->airfield_to_world(af, l, w);
            int h1 = 0, h2 = 0;
            double top = probe_down(w[0], o[1] + 60.0, w[2], &h1);
            double low = probe_down(w[0], o[1] + 5.0, w[2], &h2);
            fprintf(fp, "%.1f|%.1f ", h1 ? top - o[1] : -99.0, h2 ? low - o[1] : -99.0);
        }
        fputc('\n', fp);
    }
    fclose(fp);
    EV("scan of tile %u written to %s", tid, path);
}

static void on_tick(void *srv, void *user) {
    ++g_tick;
    if (D.scan_tile) { scan_airfield((uint32_t)D.scan_tile); D.scan_tile = 0; }
    STAT(SERVER_TICKS);
    D.server = (uint32_t)(uintptr_t)srv;
    if (D.watch_id) D.watch_ptr = (uint32_t)(uintptr_t)vehicle_by_id(D.watch_id);
    if (g_tick % 8 == 0) {
        for (int i = 0; i < MAX_PENDING; ++i) {
            if (!g_pending[i].vid) continue;
            void *v = vehicle_by_id(g_pending[i].vid);
            if (!v || dock_state(v) != CC2X_DOCK_UNDOCKED) { g_pending[i].vid = 0; continue; }
            void *tile = tile_by_id(g_pending[i].tid);
            if (!tile || GI(tile, CC2X_TILE_TEAM) != GI(v, CC2X_VEHICLE_TEAM)) { g_pending[i].vid = 0; continue; }
            void *island = A->ref(tile, CC2X_TILE_ISLAND, 0);
            if (!island) continue;                         /* not loaded yet: keep waiting */
            if (!airfield_count(island)) { g_pending[i].vid = 0; continue; }
            if (!do_land(v, island)) continue;             /* full: retry later */
            g_pending[i].vid = 0;
            STAT(LAND_QUEUED);
            A->waypoints_sync(v);                          /* clients: waypoints cleared */
        }
    }
    if (g_tick % 64 == 1) refresh_table();
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
    uint32_t id = (uint32_t)((uintptr_t)user >> 16), tid = (uint32_t)((uintptr_t)user & 0xffff);
    void *v = vehicle_by_id(id);
    if (v && land(v, tid) == 1) A->waypoints_sync(v);
}
static int lua_land(cc2x_lua *L) {
    int64_t id, tid;
    int ok = A->lua_tointeger(L, 1, &id) && A->lua_tointeger(L, 2, &tid) &&
             id > 0 && id < 0x10000 && tid > 0 && tid < 0x10000 &&
             A->server_post(land_post, (void *)(uintptr_t)((id << 16) | tid));
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
    if (api->api_version < 3 || api->size < sizeof(cc2x_api)) {
        api->log(PLUGIN, CC2X_LOG_WARN, "needs CC2X 0.4 or newer (this is %s): inactive", api->sdk_version);
        return 0;
    }
    A = api;
    InitializeCriticalSection(&g_table_cs);
    /* optional settings, kept where mod updates do not overwrite them:
     * %APPDATA%\Carrier Command 2\cc2x\island_airfields.ini   [island_airfields] planes=0 -> helicopters only */
    char ini[MAX_PATH * 2];
    snprintf(ini, sizeof(ini), "%s\\island_airfields.ini", api->config_dir);
    g_planes = GetPrivateProfileIntA("island_airfields", "planes", 1, ini);
    g_plane_assist = GetPrivateProfileIntA("island_airfields", "plane_assist", 1, ini);

    /* all or nothing: the game events this mod needs (each changes the game only through our
     * callbacks below) */
    if (!api->on_waypoints(on_waypoints, NULL) || !api->on_hangar_choice(on_hangar_choice, NULL) ||
        !api->on_island_controller(on_island_controller, NULL) || !api->on_island_spawn(on_island_spawn, NULL) ||
        !api->on_airfield_tick(on_airfield_tick, NULL)) {
        api->log(PLUGIN, CC2X_LOG_ERROR, "game events not available: inactive");
        return 0;
    }
    g_on = 1;
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
