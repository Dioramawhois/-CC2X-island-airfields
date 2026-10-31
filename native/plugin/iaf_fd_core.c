/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Island Airfields - flight director core; see iaf_fd_core.h. Formulas and their evaluation
 * order are those of the original island_airfields.c flight director. */
#include "iaf_fd_core.h"

#include <math.h>
#include <string.h>

#define P_BANK_SIGN -1.0            /* +x is the left wing: a left turn (yaw rate > 0) lowers it */

_Static_assert(sizeof(iaf_runway) == 6 * sizeof(double), "iaf_runway layout");
_Static_assert(sizeof(iaf_fd_kin) == 6 * sizeof(double), "iaf_fd_kin layout");
_Static_assert(sizeof(iaf_fd_cmd) == 6 * sizeof(double), "iaf_fd_cmd layout");

double iaf_fd_clamp(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }
double iaf_fd_wrap(double a) { return remainder(a, 6.283185307179586); }

void iaf_fd_kin_reset(iaf_fd_kin *k) {
    memset(k, 0, sizeof(*k));
    k->yaw = IAF_FD_YAW_UNSET;
}

int32_t iaf_fd_kin_seed(iaf_fd_kin *k, const double vel[3], int32_t takeoff) {
    if (!(k->yaw > 1e8)) return 0;
    double vh = sqrt(vel[0] * vel[0] + vel[2] * vel[2]);
    k->yaw = vh > 5.0 ? atan2(vel[0], vel[2]) : 0.0;
    k->speed = takeoff ? 0.0 : vh;
    k->vz = vel[1];
    k->hv[0] = vel[0]; k->hv[1] = vel[2];
    return 1;
}

void iaf_fd_kin_line_up(iaf_fd_kin *k, double yaw) {
    k->yaw = yaw; k->speed = 0.0; k->vz = 0.0; k->roll = 0.0;
}

int32_t iaf_fd_tick_dt(int64_t now, int64_t prev, int64_t freq, double *dt) {
    double d = prev ? (double)(now - prev) / (double)freq : 1.0 / 30;
    if (d <= 0.0) return 0;
    if (d > 0.1) d = 0.1;
    *dt = d;
    return 1;
}

int32_t iaf_runway_from_nodes(const double start[3], const double end[3], int32_t rev, iaf_runway *r) {
    const double *a = rev ? end : start, *b = rev ? start : end;
    double dx = b[0] - a[0], dz = b[2] - a[2], len = sqrt(dx * dx + dz * dz);
    if (len < 1.0) return 0;
    memcpy(r->thr, a, sizeof(r->thr));
    r->ax = dx / len; r->az = dz / len; r->len = len;
    return 1;
}

void iaf_runway_frame(const iaf_runway *r, double x, double z, double *s, double *lat) {
    double dx = x - r->thr[0], dz = z - r->thr[2];
    *s = -(dx * r->ax + dz * r->az);
    *lat = -dx * r->az + dz * r->ax;
}

void iaf_runway_point(const iaf_runway *r, double s, double lat, double out[3]) {
    out[0] = r->thr[0] - s * r->ax - lat * r->az;
    out[1] = r->thr[1];
    out[2] = r->thr[2] - s * r->az + lat * r->ax;
}

double iaf_runway_yaw(const iaf_runway *r) { return atan2(r->ax, r->az); }   /* cc2x_yaw_of */

double iaf_fd_steer(const iaf_fd_kin *k, const double pos[3], const double t[3], double max_rate) {
    return iaf_fd_clamp(iaf_fd_wrap(atan2(t[0] - pos[0], t[2] - pos[2]) - k->yaw) * 0.8, -max_rate, max_rate);
}

void iaf_fd_plane_fly(iaf_fd_kin *k, double y, double dt, double rate, double speed, double alt,
                      double vz_path, double sink_max, double pitch_add, iaf_fd_cmd *out) {
    k->yaw = iaf_fd_wrap(k->yaw + rate * dt);
    k->speed += iaf_fd_clamp(speed - k->speed, -5.0 * dt, 4.0 * dt);
    double vz = iaf_fd_clamp(vz_path + (alt - y) * 0.4, -sink_max, 12.0);
    k->vz += iaf_fd_clamp(vz - k->vz, -8.0 * dt, 8.0 * dt);
    double hs = sqrt(fmax(k->speed * k->speed - k->vz * k->vz, 100.0));
    out->vel[0] = sin(k->yaw) * hs; out->vel[1] = k->vz; out->vel[2] = cos(k->yaw) * hs;
    double roll = P_BANK_SIGN * atan(rate * k->speed / 9.81);
    k->roll += iaf_fd_clamp(roll - k->roll, -0.8 * dt, 0.8 * dt);
    out->yaw = k->yaw;
    out->pitch = atan2(k->vz, hs) + pitch_add;
    out->roll = k->roll;
}

int32_t iaf_fd_plane_rollout(iaf_fd_kin *k, const iaf_runway *r, double s, double lat, double dt,
                             double vy, iaf_fd_cmd *out) {
    double yl = iaf_runway_yaw(r);
    double remain = r->len + s;
    double dec = iaf_fd_clamp(k->speed * k->speed / (2.0 * fmax(remain - 30.0, 15.0)), 4.0, 20.0);
    k->speed = fmax(k->speed - dec * dt, 0.0);
    k->yaw = iaf_fd_wrap(k->yaw + iaf_fd_clamp(iaf_fd_wrap(yl - k->yaw), -0.3 * dt, 0.3 * dt));
    double side_v = iaf_fd_clamp(-lat * 0.5, -3.0, 3.0);
    out->vel[0] = sin(k->yaw) * k->speed - r->az * side_v;
    out->vel[1] = vy;
    out->vel[2] = cos(k->yaw) * k->speed + r->ax * side_v;
    out->yaw = k->yaw; out->pitch = 0.0; out->roll = 0.0;
    return k->speed < IAF_FD_ROLLOUT_STOP;
}

int32_t iaf_fd_plane_takeoff_roll(iaf_fd_kin *k, const iaf_runway *r, double lat, double dt,
                                  double vel_y, iaf_fd_cmd *out) {
    double yl = iaf_runway_yaw(r);
    k->speed += 9.0 * dt;
    k->yaw = yl;
    double side_v = iaf_fd_clamp(-lat * 0.5, -3.0, 3.0);
    out->vel[0] = sin(yl) * k->speed - r->az * side_v;
    out->vel[1] = iaf_fd_clamp(vel_y, -5.0, 1.0);
    out->vel[2] = cos(yl) * k->speed + r->ax * side_v;
    out->yaw = yl; out->pitch = 0.0; out->roll = 0.0;
    if (!(k->speed >= IAF_FD_ROTATE_SPEED)) return 0;
    k->vz = 0.0;
    return 1;
}

#define P_SPEED 75.0
#define P_FINAL_SPEED 62.0
#define P_TURN_RATE 0.14
#define P_WIDTH (2.0 * P_SPEED / P_TURN_RATE)
#define P_AIM 60.0

void iaf_landing_reset(iaf_landing_state *state, int32_t phase) {
    memset(state, 0, sizeof(*state));
    state->phase = phase;
}

void iaf_landing_step(iaf_landing_state *f, const iaf_fd_kin *k,
                      const iaf_landing_input *in, iaf_landing_output *o) {
    const iaf_runway *r = &in->runway;
    memset(o, 0, sizeof(*o));
    iaf_runway_frame(r, in->pos[0], in->pos[2], &o->s, &o->lat);
    double s = o->s, lat = o->lat, ry = r->thr[1], yl = iaf_runway_yaw(r);
    double h = o->height = in->pos[1] - ry;
    double fin = (in->cruise - in->h0) / in->slope + 600.0;
    o->speed = P_SPEED; o->alt = ry + in->cruise; o->sink = 15.0;
    iaf_runway_point(r, s, 0.0, o->target);
    switch (f->phase) {
    case PH_HOLD: {
        if (in->runway_granted) { f->phase = PH_INBOUND; o->action = IAF_LAND_RETURN; return; }
        double c[3];
        iaf_runway_point(r, fin * 0.5, f->side * (P_WIDTH + 1500.0), c);
        double ang = atan2(in->pos[0] - c[0], in->pos[2] - c[2]) + 0.6;
        o->target[0] = c[0] + sin(ang) * 700.0; o->target[2] = c[2] + cos(ang) * 700.0;
        o->rate = iaf_fd_steer(k, in->pos, o->target, P_TURN_RATE);
        break;
    }
    case PH_INBOUND: {
        if (!in->runway_granted) { f->side = lat >= 0 ? 1 : -1; f->phase = PH_HOLD; o->action = IAF_LAND_RETURN; return; }
        if (s > 0.6 * fin && fabs(lat) < 0.3 * s && fabs(iaf_fd_wrap(yl - k->yaw)) < 0.8) {
            f->phase = PH_FINAL; o->action = IAF_LAND_RETURN; return;
        }
        if (!f->side) f->side = lat >= 0 ? 1 : -1;
        int outbound = fabs(iaf_fd_wrap(yl + 3.141592653589793 - k->yaw)) < 0.6;
        if (s > -400.0 && s < fin - 300.0 && fabs(lat - f->side * P_WIDTH) < 350.0 && outbound) {
            f->phase = PH_DOWNWIND; o->action = IAF_LAND_RETURN; return;
        }
        iaf_runway_point(r, -300.0, f->side * P_WIDTH, o->target);
        o->rate = iaf_fd_steer(k, in->pos, o->target, P_TURN_RATE);
        if (hypot(o->target[0] - in->pos[0], o->target[2] - in->pos[2]) < 300.0) f->phase = PH_DOWNWIND;
        break;
    }
    case PH_DOWNWIND:
        iaf_runway_point(r, fmin(s + 800.0, fin + 600.0), f->side * P_WIDTH, o->target);
        o->rate = iaf_fd_steer(k, in->pos, o->target, P_TURN_RATE);
        if (s >= fin) f->phase = PH_BASE;
        break;
    case PH_BASE:
        o->rate = -f->side * P_TURN_RATE;
        o->speed = 70.0;
        if (fabs(iaf_fd_wrap(yl - k->yaw)) < 0.3) f->phase = PH_FINAL;
        break;
    case PH_FINAL: {
        double glide = iaf_fd_clamp(in->slope * (s + P_AIM), 0.0, in->cruise);
        int off = (s < -0.85 * r->len) || (s < 300.0 && fabs(lat) > 60.0) || (s < 800.0 && h > glide + 80.0) ||
                  (s < 1500.0 && fabs(iaf_fd_wrap(yl - k->yaw)) > 1.2);
        if (off) {
            f->go_arounds++;
            if (f->go_arounds >= 3) { o->action = IAF_LAND_ABORT; return; }
            f->side = lat >= 0 ? 1 : -1;
            f->phase = PH_INBOUND; o->action = IAF_LAND_GO_AROUND; return;
        }
        iaf_runway_point(r, s - fmax(250.0, 2.5 * fabs(lat)), 0.0, o->target);
        o->rate = iaf_fd_steer(k, in->pos, o->target, P_TURN_RATE);
        o->alt = ry + glide;
        o->speed = s < 2000.0 ? P_FINAL_SPEED : P_SPEED;
        o->sink = fmax(25.0, in->slope * P_FINAL_SPEED + 6.0);
        if (glide > 0.0 && glide < in->cruise) o->vz_path = -in->slope * k->speed;
        o->gear_down = s < 2500.0 && !f->gear_set;
        if (o->gear_down) f->gear_set = 1;
        if (s < 1000.0 && fabs(lat) > f->max_lat) f->max_lat = fabs(lat);
        o->check_touchdown = s < 150.0;
        break;
    }
    case PH_ROLLOUT:
        o->action = IAF_LAND_ROLLOUT;
        return;
    }
    /* Deliberately uses the resulting phase, not the phase at entry (BASE -> FINAL). */
    o->terrain_floor = !(f->phase == PH_FINAL && s < 600.0);
}

int32_t iaf_landing_touchdown(iaf_landing_state *f, double s, double h,
                             int32_t main_gear_contact, double total_speed) {
    if (!(s < 150.0 && (main_gear_contact || h < 2.5 || (s < 0.0 && h < 8.0)))) return 0;
    f->td_speed = total_speed;
    f->phase = PH_ROLLOUT;
    return 1;
}

void iaf_landing_rollout_begin(iaf_fd_kin *k, const double vel[3]) {
    k->speed = sqrt(vel[0] * vel[0] + vel[2] * vel[2]);
}
