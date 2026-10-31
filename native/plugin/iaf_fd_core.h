/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Island Airfields - flight director core (platform neutral).
 *
 * The kinematic state and per-tick math of the flight director, extracted unchanged from
 * island_airfields.c and called directly by the plugin. No Win32, no CC2X API, no game layouts,
 * no globals: the caller passes explicit host values (positions, velocities, runway nodes, raw
 * counter ticks) and applies the outputs (velocity / attitude commands) through the SDK.
 * World axes as in CC2X: x, z horizontal, y up, yaw 0 = nose along +z, +pi/2 = along +x.
 *
 * This is a C source-level boundary only; it does not make the plugin binary portable.
 */
#ifndef IAF_FD_CORE_H
#define IAF_FD_CORE_H

#include <stdint.h>

#define IAF_FD_CORE_ABI 2
#define IAF_FD_YAW_UNSET 1e9          /* iaf_fd_kin.yaw before the first tick */
#define IAF_FD_ROTATE_SPEED 45.0      /* takeoff roll: rotate at this speed, m/s */
#define IAF_FD_ROLLOUT_STOP 2.0       /* rollout ends (park) below this speed, m/s */

/* runway: threshold node, unit landing direction (ax, az), length; fixed-width POD */
typedef struct iaf_runway { double thr[3], ax, az, len; } iaf_runway;

/* per-flight kinematic state owned by the flight director; fixed-width POD */
typedef struct iaf_fd_kin {
    double yaw, speed, vz, roll;      /* rad, m/s, m/s, rad */
    double hv[2];                     /* horizontal velocity x, z (helicopter) */
} iaf_fd_kin;

/* a body command: velocity and attitude (yaw, pitch nose up > 0, roll); fixed-width POD */
typedef struct iaf_fd_cmd { double vel[3], yaw, pitch, roll; } iaf_fd_cmd;

double iaf_fd_clamp(double x, double lo, double hi);
double iaf_fd_wrap(double a);                     /* to [-pi, pi] */

/* lifecycle: a new flight (yaw = IAF_FD_YAW_UNSET, everything else 0) */
void iaf_fd_kin_reset(iaf_fd_kin *k);
/* first tick after reset: seed from the current motion; returns 1 when it seeded */
int32_t iaf_fd_kin_seed(iaf_fd_kin *k, const double vel[3], int32_t takeoff);
/* lined up on the runway, standing still, nose to yaw */
void iaf_fd_kin_line_up(iaf_fd_kin *k, double yaw);

/* tick length from raw counter ticks (prev 0 = first tick: 1/30 s), capped at 0.1 s.
 * Returns 0 when the tick must be skipped (dt <= 0). */
int32_t iaf_fd_tick_dt(int64_t now, int64_t prev, int64_t freq, double *dt);

/* runway from its start/end nodes; rev = 1 lands from the end node. 0 (r untouched) if < 1 m. */
int32_t iaf_runway_from_nodes(const double start[3], const double end[3], int32_t rev, iaf_runway *r);
/* s: distance before the threshold (> 0 on the approach side), lat: distance to the side */
void iaf_runway_frame(const iaf_runway *r, double x, double z, double *s, double *lat);
void iaf_runway_point(const iaf_runway *r, double s, double lat, double out[3]);
double iaf_runway_yaw(const iaf_runway *r);

/* turn rate from pos towards target t, at most max_rate */
double iaf_fd_steer(const iaf_fd_kin *k, const double pos[3], const double t[3], double max_rate);

/* plane flight: turn at rate, towards speed, climbing / sinking from height y towards alt;
 * vz_path is the vertical speed of the path itself */
void iaf_fd_plane_fly(iaf_fd_kin *k, double y, double dt, double rate, double speed, double alt,
                      double vz_path, double sink_max, double pitch_add, iaf_fd_cmd *out);

/* landing rollout, braked like by an arresting wire; vy: the host's vertical velocity choice.
 * Returns 1 when slow enough to park. out: velocity, attitude level on the runway heading. */
int32_t iaf_fd_plane_rollout(iaf_fd_kin *k, const iaf_runway *r, double s, double lat, double dt,
                             double vy, iaf_fd_cmd *out);

/* takeoff roll; vel_y: current vertical velocity. Returns 1 at rotation (then vz = 0). */
int32_t iaf_fd_plane_takeoff_roll(iaf_fd_kin *k, const iaf_runway *r, double lat, double dt,
                                  double vel_y, iaf_fd_cmd *out);

/* Phase values shared with the adapter's logging/save-adoption paths. */
enum {
    PH_NONE, PH_HOLD, PH_INBOUND, PH_DOWNWIND, PH_BASE, PH_FINAL, PH_ROLLOUT,
    PH_WAIT, PH_ROLL, PH_CLIMB, PH_H_INBOUND, PH_H_DESCEND, PH_H_LIFT
};
typedef struct iaf_landing_state {
    int32_t phase, side, gear_set, go_arounds;
    double max_lat, td_speed;
} iaf_landing_state;
typedef struct iaf_landing_input {
    iaf_runway runway;
    double pos[3], slope, h0, cruise;
    int32_t runway_granted; /* adapter acquires ONLY in HOLD / INBOUND */
} iaf_landing_input;
enum { IAF_LAND_FLY, IAF_LAND_RETURN, IAF_LAND_GO_AROUND, IAF_LAND_ABORT, IAF_LAND_ROLLOUT };
typedef struct iaf_landing_output {
    double s, lat, height, target[3], rate, speed, alt, sink, vz_path;
    int32_t action, gear_down, check_touchdown, terrain_floor;
} iaf_landing_output;

void iaf_landing_reset(iaf_landing_state *state, int32_t phase);
/* Does not acquire/release locks or apply effects. phase changes marked RETURN consume the tick;
 * other phase changes still fly this tick. GO_AROUND requests release before phase logging.
 * ABORT requests hand-back then flight end. ROLLOUT uses iaf_fd_plane_rollout then park if true.
 * In FINAL, apply gear_down BEFORE querying contact and calling iaf_landing_touchdown. */
void iaf_landing_step(iaf_landing_state *state, const iaf_fd_kin *kin,
                      const iaf_landing_input *in, iaf_landing_output *out);
int32_t iaf_landing_touchdown(iaf_landing_state *state, double s, double height,
                             int32_t main_gear_contact, double total_speed);
/* Adapter invokes after dock/ground/gear effects, matching the original touchdown sequence. */
void iaf_landing_rollout_begin(iaf_fd_kin *kin, const double vel[3]);

#endif
