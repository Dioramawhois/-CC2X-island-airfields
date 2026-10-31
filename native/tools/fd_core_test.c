/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Native regression for the production flight director core (plugin/iaf_fd_core.c).
 * Calls the same functions the plugin links; no game, world or SDK. The `ref_*` functions are the
 * pre-extraction island_airfields.c formulas, verbatim, used as a bit-exact baseline.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../plugin/iaf_fd_core.h"

#define PI 3.141592653589793
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
#define NEAR(a, b, e) CHECK(fabs((a) - (b)) <= (e))
static int same(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }

/* ---- old formulas (island_airfields.c before extraction) ---- */
static double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }
static double wrap(double a) { return remainder(a, 6.283185307179586); }
typedef struct { double yaw, speed, vz, roll; } Old;
static void ref_fly(Old *f, double y, double dt, double rate, double speed, double alt, double vz_path,
                    double sink_max, double pitch_add, double nv[3], double att[3]) {
    f->yaw = wrap(f->yaw + rate * dt);
    f->speed += clampd(speed - f->speed, -5.0 * dt, 4.0 * dt);
    double vz = clampd(vz_path + (alt - y) * 0.4, -sink_max, 12.0);
    f->vz += clampd(vz - f->vz, -8.0 * dt, 8.0 * dt);
    double hs = sqrt(fmax(f->speed * f->speed - f->vz * f->vz, 100.0));
    nv[0] = sin(f->yaw) * hs; nv[1] = f->vz; nv[2] = cos(f->yaw) * hs;
    double roll = -1.0 * atan(rate * f->speed / 9.81);
    f->roll += clampd(roll - f->roll, -0.8 * dt, 0.8 * dt);
    att[0] = f->yaw; att[1] = atan2(f->vz, hs) + pitch_add; att[2] = f->roll;
}
static void ref_rollout(Old *f, const iaf_runway *r, double s, double lat, double dt, double vy, double nv[3]) {
    double yl = atan2(r->ax, r->az), remain = r->len + s;
    double dec = clampd(f->speed * f->speed / (2.0 * fmax(remain - 30.0, 15.0)), 4.0, 20.0);
    f->speed = fmax(f->speed - dec * dt, 0.0);
    f->yaw = wrap(f->yaw + clampd(wrap(yl - f->yaw), -0.3 * dt, 0.3 * dt));
    double side_v = clampd(-lat * 0.5, -3.0, 3.0);
    nv[0] = sin(f->yaw) * f->speed - r->az * side_v; nv[1] = vy; nv[2] = cos(f->yaw) * f->speed + r->ax * side_v;
}

static void test_runway(void) {
    iaf_runway r, keep;
    double a[3] = {100.0, 12.5, -50.0}, b[3] = {100.0 + 1200.0, 30.0, -50.0 + 500.0};
    CHECK(iaf_runway_from_nodes(a, b, 0, &r));
    NEAR(r.len, 1300.0, 1e-9); NEAR(r.ax * r.ax + r.az * r.az, 1.0, 1e-15);
    CHECK(r.thr[1] == 12.5);                                          /* threshold height, start node */
    CHECK(iaf_runway_from_nodes(a, b, 1, &keep));
    CHECK(keep.thr[0] == b[0] && keep.ax == -r.ax && keep.az == -r.az);
    NEAR(iaf_fd_wrap(iaf_runway_yaw(&keep) - iaf_runway_yaw(&r) - PI), 0.0, 1e-12);
    /* zero / sub-metre runway: rejected, output untouched (y difference is ignored) */
    memset(&keep, 0x5a, sizeof keep);
    iaf_runway copy = keep;
    double c[3] = {100.0 + 0.6, 500.0, -50.0 + 0.6};
    CHECK(!iaf_runway_from_nodes(a, a, 0, &keep) && !iaf_runway_from_nodes(a, c, 0, &keep));
    CHECK(!memcmp(&keep, &copy, sizeof keep));
    /* frame <-> point round trip; s > 0 on the approach side, +lat to the left (-az, ax) */
    double p[3], s, lat;
    iaf_runway_point(&r, 700.0, -250.0, p);
    iaf_runway_frame(&r, p[0], p[2], &s, &lat);
    NEAR(s, 700.0, 1e-9); NEAR(lat, -250.0, 1e-9); CHECK(p[1] == r.thr[1]);
    iaf_runway_frame(&r, r.thr[0], r.thr[2], &s, &lat);
    CHECK(s == 0.0 && lat == 0.0);                                     /* zero distance */
    iaf_runway_frame(&r, r.thr[0] + r.ax * 10.0, r.thr[2] + r.az * 10.0, &s, &lat);
    NEAR(s, -10.0, 1e-12);                                            /* past the threshold: s < 0 */
}

static void test_lifecycle(void) {
    iaf_fd_kin k;
    memset(&k, 0x77, sizeof k);
    iaf_fd_kin_reset(&k);
    CHECK(k.yaw == IAF_FD_YAW_UNSET && k.speed == 0.0 && k.vz == 0.0 && k.roll == 0.0 && k.hv[0] == 0.0 && k.hv[1] == 0.0);
    double v[3] = {30.0, -2.0, 40.0};
    CHECK(iaf_fd_kin_seed(&k, v, 0));
    NEAR(k.speed, 50.0, 1e-12); NEAR(k.yaw, atan2(30.0, 40.0), 0.0); CHECK(k.vz == -2.0 && k.hv[1] == 40.0);
    CHECK(!iaf_fd_kin_seed(&k, (double[3]){0, 0, 0}, 0) && k.speed == 50.0);   /* once only */
    iaf_fd_kin_reset(&k);
    CHECK(iaf_fd_kin_seed(&k, (double[3]){3.0, 1.0, 4.0}, 1));
    CHECK(k.yaw == 0.0 && k.speed == 0.0 && k.vz == 1.0);              /* <= 5 m/s: yaw 0; takeoff: speed 0 */
    iaf_fd_kin_reset(&k);
    CHECK(iaf_fd_kin_seed(&k, (double[3]){0, 0, 0}, 0) && k.yaw == 0.0 && k.speed == 0.0);
    k.roll = 0.3; k.vz = 2.0; k.speed = 9.0;
    iaf_fd_kin_line_up(&k, 1.25);
    CHECK(k.yaw == 1.25 && k.speed == 0.0 && k.vz == 0.0 && k.roll == 0.0);
}

static void test_dt(void) {
    double dt = -1.0;
    CHECK(iaf_fd_tick_dt(500, 0, 1000, &dt)); NEAR(dt, 1.0 / 30, 0.0);   /* first tick */
    CHECK(iaf_fd_tick_dt(1050, 1000, 1000, &dt) && dt == 0.05);
    CHECK(iaf_fd_tick_dt(9000, 1000, 1000, &dt) && dt == 0.1);           /* capped */
    dt = 7.0;
    CHECK(!iaf_fd_tick_dt(1000, 1000, 1000, &dt) && dt == 7.0);          /* zero: skipped */
    CHECK(!iaf_fd_tick_dt(900, 1000, 1000, &dt) && dt == 7.0);           /* backwards: skipped */
}

static void test_fly_equivalence(void) {
    /* recorded-style tick sequence over turns, climbs, sinks and dt edges; bit-exact vs old code */
    static const double ticks[][8] = {
        /* y, dt, rate, speed, alt, vz_path, sink_max, pitch_add */
        {300.0, 1.0 / 30, 0.14, 75.0, 270.0, 0.0, 15.0, 0.0},
        {280.0, 0.1, -0.14, 62.0, 100.0, -15.5, 25.0, 0.05},
        {40.0, 0.016, 0.0, 62.0, 20.0, -16.0, 25.0, 0.0},
        {2000.0, 0.1, 0.3, 75.0, 10.0, 0.0, 15.0, 0.0},
        {-5.0, 0.1, -0.3, 75.0, 600.0, 0.0, 15.0, 0.0},
        {250.0, 1e-6, 0.14, 0.0, 250.0, 0.0, 15.0, 0.0},
    };
    iaf_fd_kin k; Old o = {3.0, 20.0, 0.0, 0.0};
    iaf_fd_kin_reset(&k); k.yaw = o.yaw; k.speed = o.speed;
    for (int rep = 0; rep < 200; ++rep)
        for (unsigned i = 0; i < sizeof ticks / sizeof ticks[0]; ++i) {
            const double *t = ticks[i];
            iaf_fd_cmd c; double nv[3], att[3];
            iaf_fd_plane_fly(&k, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], &c);
            ref_fly(&o, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], nv, att);
            CHECK(same(k.yaw, o.yaw) && same(k.speed, o.speed) && same(k.vz, o.vz) && same(k.roll, o.roll));
            CHECK(same(c.vel[0], nv[0]) && same(c.vel[1], nv[1]) && same(c.vel[2], nv[2]));
            CHECK(same(c.yaw, att[0]) && same(c.pitch, att[1]) && same(c.roll, att[2]));
            CHECK(isfinite(c.vel[0]) && isfinite(c.pitch) && fabs(k.yaw) <= PI);
        }
    /* invariants: horizontal speed >= 10 m/s even at speed 0; bank sign: left turn lowers +x wing */
    iaf_fd_kin_reset(&k); k.yaw = 0.0;
    iaf_fd_cmd c;
    iaf_fd_plane_fly(&k, 100.0, 0.1, 0.14, 0.0, 100.0, 0.0, 15.0, 0.0, &c);
    NEAR(hypot(c.vel[0], c.vel[2]), 10.0, 1e-12);
    CHECK(k.roll == 0.0);                                             /* speed 0: no bank */
    k.speed = 75.0;
    iaf_fd_plane_fly(&k, 100.0, 0.1, 0.14, 75.0, 100.0, 0.0, 15.0, 0.0, &c);
    CHECK(c.roll < 0.0 && c.roll >= -0.08 - 1e-15);                   /* slew limit 0.8 rad/s */
    /* vertical: commanded vz capped at +12 m/s and -sink_max, slewed 8 m/s^2 */
    iaf_fd_kin_reset(&k); k.yaw = 0.0; k.speed = 75.0;
    for (int i = 0; i < 50; ++i) iaf_fd_plane_fly(&k, 0.0, 0.1, 0.0, 75.0, 1e6, 0.0, 15.0, 0.0, &c);
    CHECK(k.vz == 12.0);
    for (int i = 0; i < 50; ++i) iaf_fd_plane_fly(&k, 1e6, 0.1, 0.0, 75.0, 0.0, 0.0, 15.0, 0.0, &c);
    CHECK(k.vz == -15.0);
}

static void test_steer(void) {
    iaf_fd_kin k; iaf_fd_kin_reset(&k);
    double pos[3] = {0, 0, 0};
    k.yaw = 0.0;
    CHECK(iaf_fd_steer(&k, pos, (double[3]){0, 0, 100}, 0.14) == 0.0);       /* dead ahead */
    CHECK(iaf_fd_steer(&k, pos, (double[3]){100, 0, 0}, 0.14) == 0.14);      /* +x: positive, capped */
    NEAR(iaf_fd_steer(&k, pos, (double[3]){1000, 0, 10000}, 10.0), atan2(1000.0, 10000.0) * 0.8, 1e-15);
    k.yaw = PI - 0.01;                                                         /* across the +-pi seam */
    CHECK(iaf_fd_steer(&k, pos, (double[3]){-1, 0, -100}, 10.0) > 0.0);
    CHECK(isfinite(iaf_fd_steer(&k, pos, pos, 0.14)));                         /* zero distance */
}

static void test_rollout(void) {
    iaf_runway r;
    double a[3] = {0, 10, 0}, b[3] = {0, 10, 1500};
    CHECK(iaf_runway_from_nodes(a, b, 0, &r));
    iaf_fd_kin k; Old o;
    iaf_fd_kin_reset(&k);
    k.yaw = o.yaw = 0.2; k.speed = o.speed = 60.0; k.vz = o.vz = 0; k.roll = o.roll = 0;
    double s = -60.0, lat = 8.0, dt = 1.0 / 30, prev = k.speed;
    int n = 0, done = 0;
    while (!done && n < 10000) {
        iaf_fd_cmd c; double nv[3];
        done = iaf_fd_plane_rollout(&k, &r, s, lat, dt, -1.0, &c);
        ref_rollout(&o, &r, s, lat, dt, -1.0, nv);
        CHECK(same(k.speed, o.speed) && same(k.yaw, o.yaw));
        CHECK(same(c.vel[0], nv[0]) && same(c.vel[1], nv[1]) && same(c.vel[2], nv[2]));
        CHECK(c.pitch == 0.0 && c.roll == 0.0 && k.speed <= prev && k.speed >= 0.0);
        CHECK(prev - k.speed >= 4.0 * dt - 1e-12 || k.speed == 0.0);   /* at least 4 m/s^2 */
        CHECK(prev - k.speed <= 20.0 * dt + 1e-12);                    /* at most 20 m/s^2 */
        prev = k.speed;
        s -= k.speed * dt; lat += c.vel[0] * dt;                       /* along +z, lateral correction */
        ++n;
    }
    CHECK(done && k.speed < IAF_FD_ROLLOUT_STOP && fabs(k.yaw) < 0.2);
    CHECK(-s < r.len);                                                  /* stopped on the runway */
    /* runway end already passed: deceleration denominator floor 15 m keeps it finite, capped 20 */
    iaf_fd_kin_reset(&k); k.yaw = 0.0; k.speed = 30.0;
    iaf_fd_cmd c;
    iaf_fd_plane_rollout(&k, &r, -5000.0, 0.0, 0.1, 0.0, &c);
    CHECK(k.speed == 28.0);
    /* side correction capped at 3 m/s toward the centre line */
    iaf_fd_kin_reset(&k); k.yaw = 0.0; k.speed = 0.0;
    CHECK(iaf_fd_plane_rollout(&k, &r, 0.0, 100.0, 0.1, 0.0, &c));
    CHECK(c.vel[0] == 3.0 && c.vel[2] == 0.0);                          /* left (+lat = -x here) -> +x */
}

static void test_takeoff_roll(void) {
    iaf_runway r;
    double a[3] = {0, 10, 0}, b[3] = {2000, 10, 0};
    CHECK(iaf_runway_from_nodes(a, b, 0, &r));
    iaf_fd_kin k; iaf_fd_kin_reset(&k);
    iaf_fd_kin_line_up(&k, iaf_runway_yaw(&r));
    k.vz = 3.0;                                                         /* must be zeroed on rotate */
    double dt = 0.1;
    int n = 0, rot = 0;
    iaf_fd_cmd c;
    while (!rot && n < 1000) { rot = iaf_fd_plane_takeoff_roll(&k, &r, 0.0, dt, 9.0, &c); ++n; }
    CHECK(rot && k.vz == 0.0 && k.speed >= IAF_FD_ROTATE_SPEED);
    int m = 0; double acc = 0.0;                                        /* same 9 m/s^2 steps */
    while (acc < IAF_FD_ROTATE_SPEED) { acc += 9.0 * dt; ++m; }
    CHECK(n == m && same(k.speed, acc) && (n == 50 || n == 51));
    CHECK(c.vel[1] == 1.0 && c.yaw == iaf_runway_yaw(&r));              /* vy clamped to [-5, 1] */
    NEAR(c.vel[0], k.speed, 1e-12); NEAR(c.vel[2], 0.0, 1e-12);
    iaf_fd_kin_reset(&k); iaf_fd_kin_line_up(&k, 0.0); k.vz = 3.0;
    CHECK(!iaf_fd_plane_takeoff_roll(&k, &r, -40.0, dt, -9.0, &c) && k.vz == 3.0);
    CHECK(c.vel[1] == -5.0 && c.vel[2] == 3.0);                         /* lat -40 (-z side) -> +z, capped */
    k.speed = NAN; k.vz = 3.0;
    CHECK(!iaf_fd_plane_takeoff_roll(&k, &r, 0.0, dt, 0.0, &c));
    CHECK(isnan(k.speed) && k.vz == 3.0); /* old positive >= predicate: NaN must not rotate */
}

/* Pre-extraction landing switch; SDK effects replaced by effect markers only.
 * Numeric/control-flow baseline independent of iaf_landing_step. */
static void ref_landing(iaf_landing_state *f, const iaf_fd_kin *k,
                        const iaf_landing_input *in, iaf_landing_output *o) {
    const double P_SPEED = 75.0, P_FINAL_SPEED = 62.0, P_TURN_RATE = 0.14;
    const double P_WIDTH = 2.0 * P_SPEED / P_TURN_RATE, P_AIM = 60.0;
    iaf_runway r = in->runway;
    memset(o, 0, sizeof(*o));
    double s, lat, t[3];
    iaf_runway_frame(&r, in->pos[0], in->pos[2], &s, &lat);
    o->s = s; o->lat = lat;
    double ry = r.thr[1], h = in->pos[1] - ry, yl = iaf_runway_yaw(&r);
    o->height = h;
    double fin = (in->cruise - in->h0) / in->slope + 600.0;
    double rate = 0.0, speed = P_SPEED, alt = ry + in->cruise, sink = 15.0, vz_path = 0.0;
    iaf_runway_point(&r, s, 0.0, t);
    switch (f->phase) {
    case PH_HOLD: {                                   /* runway busy: orbit beside the pattern */
        if (in->runway_granted) { f->phase = PH_INBOUND; o->action = IAF_LAND_RETURN; return; }
        double c[3];
        iaf_runway_point(&r, fin * 0.5, f->side * (P_WIDTH + 1500.0), c);
        double ang = atan2(in->pos[0] - c[0], in->pos[2] - c[2]) + 0.6;
        t[0] = c[0] + sin(ang) * 700.0; t[2] = c[2] + cos(ang) * 700.0;
        rate = iaf_fd_steer(k, in->pos, t, P_TURN_RATE);
        break;
    }
    case PH_INBOUND: {
        if (!in->runway_granted) { f->side = lat >= 0 ? 1 : -1; f->phase = PH_HOLD; o->action = IAF_LAND_RETURN; return; }
        if (s > 0.6 * fin && fabs(lat) < 0.3 * s && fabs(wrap(yl - k->yaw)) < 0.8) { f->phase = PH_FINAL; o->action = IAF_LAND_RETURN; return; }
        if (!f->side) f->side = lat >= 0 ? 1 : -1;
        int outbound = fabs(wrap(yl + 3.141592653589793 - k->yaw)) < 0.6;
        if (s > -400.0 && s < fin - 300.0 && fabs(lat - f->side * P_WIDTH) < 350.0 && outbound) { f->phase = PH_DOWNWIND; o->action = IAF_LAND_RETURN; return; }
        iaf_runway_point(&r, -300.0, f->side * P_WIDTH, t);       /* downwind entry, abeam the runway */
        rate = iaf_fd_steer(k, in->pos, t, P_TURN_RATE);
        if (hypot(t[0] - in->pos[0], t[2] - in->pos[2]) < 300.0) f->phase = PH_DOWNWIND;
        break;
    }
    case PH_DOWNWIND:
        iaf_runway_point(&r, fmin(s + 800.0, fin + 600.0), f->side * P_WIDTH, t);
        rate = iaf_fd_steer(k, in->pos, t, P_TURN_RATE);
        if (s >= fin) f->phase = PH_BASE;
        break;
    case PH_BASE:                                     /* full-rate 180 deg turn towards the axis */
        rate = -f->side * P_TURN_RATE;
        speed = 70.0;
        if (fabs(wrap(yl - k->yaw)) < 0.3) f->phase = PH_FINAL;
        break;
    case PH_FINAL: {
        double glide = clampd(in->slope * (s + P_AIM), 0.0, in->cruise);
        int off = (s < -0.85 * r.len) || (s < 300.0 && fabs(lat) > 60.0) || (s < 800.0 && h > glide + 80.0) ||
                  (s < 1500.0 && fabs(wrap(yl - k->yaw)) > 1.2);
        if (off) {                                    /* go round: the pattern again */
            f->go_arounds++;
            if (f->go_arounds >= 3) {
                o->action = IAF_LAND_ABORT;
                return;
            }

            f->side = lat >= 0 ? 1 : -1;
            f->phase = PH_INBOUND;
            o->action = IAF_LAND_GO_AROUND;
            return;
        }
        iaf_runway_point(&r, s - fmax(250.0, 2.5 * fabs(lat)), 0.0, t);
        rate = iaf_fd_steer(k, in->pos, t, P_TURN_RATE);
        alt = ry + glide;
        speed = s < 2000.0 ? P_FINAL_SPEED : P_SPEED;
        sink = fmax(25.0, in->slope * P_FINAL_SPEED + 6.0);
        if (glide > 0.0 && glide < in->cruise) vz_path = -in->slope * k->speed;
        if (s < 2500.0 && !f->gear_set) { o->gear_down = 1; f->gear_set = 1; }
        if (s < 1000.0 && fabs(lat) > f->max_lat) f->max_lat = fabs(lat);
        o->check_touchdown = s < 150.0;
        break;
    }
    case PH_ROLLOUT: o->action = IAF_LAND_ROLLOUT; return;
    }
    memcpy(o->target, t, sizeof(t));
    o->rate = rate; o->speed = speed; o->alt = alt; o->sink = sink; o->vz_path = vz_path;
    o->terrain_floor = !(f->phase == PH_FINAL && s < 600.0);
}

static void test_landing(void) {
    iaf_landing_input in = {0};
    double start[3] = {100, 20, -100}, end[3] = {100, 20, 1400};
    CHECK(iaf_runway_from_nodes(start, end, 0, &in.runway));
    in.slope = 0.25; in.h0 = 20; in.cruise = 250;
    iaf_fd_kin k; iaf_fd_kin_reset(&k); k.yaw = 0; k.speed = 70;
    /* Each row: entry phase, s, lat, height, yaw, granted, side, retries,
     * expected phase, action. No world model: explicit director observations. */
    const double rows[][10] = {
        {PH_HOLD, 2000, 1200, 250, 0, 0, 1, 0, PH_HOLD, IAF_LAND_FLY},
        {PH_HOLD, 2000, -1200, 250, 0, 1, -1, 0, PH_INBOUND, IAF_LAND_RETURN},
        {PH_INBOUND, 2000, -10, 250, 0, 0, 0, 0, PH_HOLD, IAF_LAND_RETURN},
        {PH_INBOUND, 2000, 0, 250, 0, 1, 0, 0, PH_FINAL, IAF_LAND_RETURN},
        {PH_INBOUND, 0, 2*75/0.14, 250, PI, 1, 1, 0, PH_DOWNWIND, IAF_LAND_RETURN},
        {PH_INBOUND, 0, -2*75/0.14, 250, PI, 1, -1, 0, PH_DOWNWIND, IAF_LAND_RETURN},
        {PH_INBOUND, -300, 2*75/0.14, 250, 0, 1, 1, 0, PH_DOWNWIND, IAF_LAND_FLY},
        {PH_DOWNWIND, 1519, 1000, 250, PI, 0, 1, 0, PH_DOWNWIND, IAF_LAND_FLY},
        {PH_DOWNWIND, 1520, 1000, 250, PI, 0, 1, 0, PH_BASE, IAF_LAND_FLY},
        {PH_BASE, 1000, 100, 250, 0.4, 0, -1, 0, PH_BASE, IAF_LAND_FLY},
        {PH_BASE, 500, 0, 100, 0.29, 0, 1, 0, PH_FINAL, IAF_LAND_FLY},
        {PH_FINAL, 250, 61, 50, 0, 0, 1, 0, PH_INBOUND, IAF_LAND_GO_AROUND},
        {PH_FINAL, 250, -61, 50, 0, 0, -1, 1, PH_INBOUND, IAF_LAND_GO_AROUND},
        {PH_FINAL, 250, 61, 50, 0, 0, 1, 2, PH_FINAL, IAF_LAND_ABORT},
        {PH_FINAL, -1276, 0, 0, 0, 0, 1, 0, PH_INBOUND, IAF_LAND_GO_AROUND},
        {PH_FINAL, 799, 0, 295, 0, 0, 1, 0, PH_INBOUND, IAF_LAND_GO_AROUND},
        {PH_FINAL, 1499, 0, 250, 1.21, 0, 1, 0, PH_INBOUND, IAF_LAND_GO_AROUND},
        {PH_FINAL, 200, 60, 65, 0, 0, 1, 0, PH_FINAL, IAF_LAND_FLY},
        {PH_FINAL, 2000, 0, 250, 0, 0, 1, 0, PH_FINAL, IAF_LAND_FLY},
        {PH_FINAL, 2500, 0, 250, 0, 0, 1, 0, PH_FINAL, IAF_LAND_FLY},
        {PH_FINAL, 100, 0, 10, 0, 0, 1, 0, PH_FINAL, IAF_LAND_FLY},
        {PH_ROLLOUT, -100, 0, 3, 0, 0, 1, 0, PH_ROLLOUT, IAF_LAND_ROLLOUT}
    };
    for (unsigned i = 0; i < sizeof(rows)/sizeof(rows[0]); ++i) {
        const double *v = rows[i];
        iaf_landing_state st, ref;
        iaf_landing_reset(&st, (int32_t)v[0]); st.side = (int32_t)v[6]; st.go_arounds = (int32_t)v[7];
        ref = st;
        iaf_runway_point(&in.runway, v[1], v[2], in.pos); in.pos[1] += v[3];
        k.yaw = v[4]; in.runway_granted = (int32_t)v[5];
        iaf_landing_output o, baseline;
        iaf_landing_step(&st, &k, &in, &o); ref_landing(&ref, &k, &in, &baseline);
        CHECK(st.phase == (int32_t)v[8] && o.action == (int32_t)v[9]);
        CHECK(!memcmp(&st, &ref, sizeof st));
        CHECK(o.action == baseline.action);
        if (o.action == IAF_LAND_FLY) CHECK(!memcmp(&o, &baseline, sizeof o));
        if (o.action == IAF_LAND_GO_AROUND) CHECK(st.side == (v[2] >= 0 ? 1 : -1));
        if (i == 10) CHECK(!o.terrain_floor); /* BASE -> FINAL uses resulting phase */
        if (i == 18) CHECK(o.speed == 75 && o.gear_down && !o.check_touchdown);
        if (i == 19) CHECK(!o.gear_down); /* strict <2500 */
        if (i == 20) CHECK(o.check_touchdown && o.gear_down);
    }
    /* Cross-product parity near strict boundaries, both sides, both admission results.
     * Compare state and every fly output byte against the old production switch. */
    const double distances[] = {-1275, -400, -300, 0, 149, 150, 299, 300, 599, 600, 799, 800, 999, 1000, 1499, 1500, 1520, 1999, 2000, 2499, 2500};
    const double lateral[] = {-1200, -61, -60, 0, 60, 61, 1200};
    for (int ph = PH_HOLD; ph <= PH_ROLLOUT; ++ph)
      for (unsigned i = 0; i < sizeof distances/sizeof distances[0]; ++i)
       for (unsigned j = 0; j < sizeof lateral/sizeof lateral[0]; ++j)
        for (int granted = 0; granted <= 1; ++granted) {
            iaf_landing_state st, ref; iaf_landing_reset(&st, ph);
            st.side = j % 2 ? -1 : 1; st.gear_set = granted; st.go_arounds = (int32_t)(i % 3); ref = st;
            iaf_runway_point(&in.runway, distances[i], lateral[j], in.pos);
            in.pos[1] += i % 2 ? 250 : 10;
            in.runway_granted = granted; k.yaw = j % 2 ? PI : 0;
            iaf_landing_output o, baseline;
            iaf_landing_step(&st, &k, &in, &o); ref_landing(&ref, &k, &in, &baseline);
            CHECK(!memcmp(&st, &ref, sizeof st) && o.action == baseline.action);
            if (o.action == IAF_LAND_FLY) CHECK(!memcmp(&o, &baseline, sizeof o));
        }
    iaf_landing_state st;
    iaf_landing_reset(&st, PH_FINAL);
    CHECK(!iaf_landing_touchdown(&st, 150, 0, 1, 62)); /* strict s<150 */
    CHECK(!iaf_landing_touchdown(&st, 100, 2.5, 0, 62));
    CHECK(!iaf_landing_touchdown(&st, 0, 7, 0, 62));
    CHECK(!iaf_landing_touchdown(&st, -1, 8, 0, 62));
    CHECK(iaf_landing_touchdown(&st, -1, 7.99, 0, 62));
    CHECK(st.phase == PH_ROLLOUT && st.td_speed == 62);
    iaf_landing_reset(&st, PH_FINAL);
    CHECK(iaf_landing_touchdown(&st, 149, 100, 1, 63));
    iaf_landing_reset(&st, PH_FINAL);
    CHECK(iaf_landing_touchdown(&st, 100, 2.49, 0, 64));
    CHECK(!iaf_landing_touchdown(&st, NAN, 0, 1, 0));
    iaf_landing_rollout_begin(&k, (double[3]){30, -10, 40});
    CHECK(k.speed == 50);
}

int main(void) {
    test_runway(); test_lifecycle(); test_dt(); test_fly_equivalence(); test_steer();
    test_rollout(); test_takeoff_roll(); test_landing();
    if (fails) { fprintf(stderr, "fd_core_test: %d failure(s)\n", fails); return 1; }
    puts("fd_core_test: ok");
    return 0;
}
