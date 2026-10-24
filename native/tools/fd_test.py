# SPDX-License-Identifier: GPL-3.0-or-later
"""Flight director acceptance test: every own aircraft parked in an island hangar takes off, flies
out, comes back, lands and parks again. Results come from the plugin's event log (cc2x.log).

usage: fd_test.py [vehicle ids...]     (default: all parked own aircraft, team 1)
"""
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iaf  # noqa: E402

LOG = r'C:\Program Files (x86)\Steam\steamapps\common\Carrier Command 2\cc2x.log'
AIR = {8: 'plane', 10: 'plane (heavy)', 12: 'helicopter', 14: 'helicopter (heavy)'}
OUT_DIST = 2500.0              # takeoff waypoint: this far from the island, both axes
OUT_TIME = 20.0                # fly out this long after the takeoff before the landing order
TAKEOFF_TIMEOUT = 150.0
LANDING_TIMEOUT = 480.0


def own_tiles(g, team=1):
    out = []
    for t in g.deque(g.server() + 0x440):
        if not t or g.u32(t + 0x60) != team:
            continue
        isl = g.u32(t + 0x100)
        if isl and g.u32(isl + 0x154 + 0xc):
            out.append((g.u32(t), g.f64(t + 0x48), g.f64(t + 0x58)))
    return out


def test_aircraft(g, team=1):
    """parked in an island hangar: takeoff + landing; flying free: landing on the nearest airfield"""
    tiles = own_tiles(g, team)
    out = []
    for v in g.deque(g.server() + 0x37c):
        if not v or g.u32(v + 0x108) != team or g.u32(v + 0x84) not in AIR or g.u32(v + 0x10c):
            continue                                        # not ours / not air / on a carrier
        dock = g.u32(v + 0x174)
        if dock not in (0, 4):
            continue
        bd, bc, bh = struct.unpack('<3I', g.p.read(v + 0x8c, 12))
        pos = struct.unpack('<3d', g.p.read(g.u32(bd + 4 * (bh % bc)) + 0x48, 24))
        near = min(tiles, key=lambda t: (t[1] - pos[0]) ** 2 + (t[2] - pos[2]) ** 2, default=None)
        if not near:
            continue
        parked = dock == 4 and (near[1] - pos[0]) ** 2 + (near[2] - pos[2]) ** 2 < 3000 ** 2
        if dock == 4 and not parked:
            continue
        out.append({'vid': g.u32(v + 0x80), 'kind': AIR[g.u32(v + 0x84)], 'tile': near[0],
                    'tx': near[1], 'ty': near[2], 'parked': parked})
    return out


def main():
    g = iaf.Game()
    planes = test_aircraft(g)
    if len(sys.argv) > 1:
        want = {int(x) for x in sys.argv[1:]}
        planes = [p for p in planes if p['vid'] in want]
    if not planes:
        raise SystemExit('no aircraft to test')
    log = open(LOG, encoding='utf-8', errors='replace')
    log.seek(0, 2)
    for p in planes:
        p.update(t=time.time(), result=None, events=[])
        if p['parked']:
            p['state'] = 'takeoff'
            ok = g.command(1, p['vid'], 0, int(p['tx'] + OUT_DIST), int(p['ty'] + OUT_DIST))
            print('%5d %-18s tile %3d: takeoff ordered%s' % (p['vid'], p['kind'], p['tile'], '' if ok else ' (NOT consumed)'), flush=True)
        else:
            p['state'], p['t_out'] = 'out', time.time() - OUT_TIME   # flying: land at once
            print('%5d %-18s flying: landing on tile %d' % (p['vid'], p['kind'], p['tile']), flush=True)
    while any(p['state'] != 'done' for p in planes):
        time.sleep(1.0)
        for line in log.readlines():
            m = re.search(r'\[island_airfields\] info: vehicle (\d+): (.*)', line)
            if not m:
                continue
            p = next((q for q in planes if q['vid'] == int(m.group(1))), None)
            if not p:
                continue
            msg = m.group(2).strip()
            p['events'].append('%.0fs %s' % (time.time() - p['t'], msg))
            print('%5d  %s' % (p['vid'], msg), flush=True)
            if msg.startswith('TOOK OFF'):
                p['state'], p['t_out'] = 'out', time.time()
            elif msg.startswith('LANDED'):
                p['state'], p['result'], p['t_park'] = 'verify', msg, time.time()   # still alive in the hangar?
            elif msg.startswith(('landing ABORTED', 'LOST')):
                p['state'], p['result'] = 'done', msg
        now = time.time()
        for p in planes:
            if p['state'] == 'verify' and now - p['t_park'] > 4.0:
                v = g.vehicle_ptr(p['vid'])
                if not v or g.u32(v + 0x174) != 4:
                    p['result'] = 'DESTROYED after parking: ' + p['result']
                p['state'] = 'done'
            if p['state'] == 'takeoff' and now - p['t'] > TAKEOFF_TIMEOUT:
                p['state'], p['result'] = 'done', 'FAILED: no takeoff within %.0f s' % TAKEOFF_TIMEOUT
            elif p['state'] == 'out' and now - p['t_out'] > OUT_TIME:
                ok = g.command(2, p['vid'], p['tile'], int(p['tx']), int(p['ty']))
                print('%5d  landing ordered on tile %d%s' % (p['vid'], p['tile'], '' if ok else ' (NOT consumed)'), flush=True)
                p['state'], p['t_land'] = 'landing', now
            elif p['state'] == 'landing' and now - p['t_land'] > LANDING_TIMEOUT:
                p['state'], p['result'] = 'done', 'FAILED: not landed within %.0f s' % LANDING_TIMEOUT
    print('\n==== results')
    failed = 0
    for p in planes:
        r = p['result']
        if r.startswith('DESTROYED'):
            ok = False
        elif r.startswith('LANDED (plane)'):
            ok = 'go-arounds 0' in r
        elif r.startswith('LANDED (helicopter)'):
            m = re.search(r'descent (\d+) s', r)
            ok = bool(m) and int(m.group(1)) <= 20          # no hanging over the pad
        else:
            ok = False
        if not ok:
            failed += 1
        took = next((e for e in p['events'] if 'TOOK OFF' in e), 'no takeoff')
        print('%s %5d %-18s tile %3d  %s | %s' % ('PASS' if ok else 'FAIL', p['vid'], p['kind'], p['tile'], took, r))
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
