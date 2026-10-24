# SPDX-License-Identifier: GPL-3.0-or-later
"""Watch one vehicle closely: dock state, hp, height, speed, wheel contact values (every 0.2 s).
usage: hp_trace.py <vid> <secs> [start_when_dock_state]"""
import math
import struct
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iaf  # noqa: E402

g = iaf.Game()
vid, secs = int(sys.argv[1]), float(sys.argv[2])
start_state = int(sys.argv[3]) if len(sys.argv) > 3 else None
end = time.time() + secs
started = start_state is None
prev = None
while time.time() < end:
    v = g.vehicle_ptr(vid)
    if not v:
        print(time.strftime('%H:%M:%S'), 'vehicle gone')
        break
    dock = g.u32(v + 0x174)
    if not started:
        if dock != start_state:
            time.sleep(0.2)
            continue
        started = True
    bd, bc, bh = struct.unpack('<3I', g.p.read(v + 0x8c, 12))
    body = g.u32(bd + 4 * (bh % bc))
    pos = struct.unpack('<3d', g.p.read(body + 0x48, 24))
    wp, wn = g.u32(v + 0xac), g.u32(v + 0xb0)
    wheels = [g.f32(wp + 0x70 * i) for i in range(min(wn, 6))] if wp else []
    spd = ''
    if prev:
        spd = '%5.1f m/s vy %5.1f' % (math.dist(pos[0::2], prev[0::2]) / 0.2, (pos[1] - prev[1]) / 0.2)
    prev = pos
    print('%s dock=%d hp=%d y=%7.2f %s wheels(n=%d)=%s' % (time.strftime('%H:%M:%S'), dock, g.u32(v + 0x180), pos[1], spd,
                                                          wn, ' '.join('%.3f' % w for w in wheels)), flush=True)
    time.sleep(0.2)
