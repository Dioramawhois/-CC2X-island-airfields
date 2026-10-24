# SPDX-License-Identifier: GPL-3.0-or-later
"""Trace an aircraft in airfield-local coordinates: position, nose heading, velocity heading, airfield
mode/state. usage: takeoff_trace.py <vid> <tile> <secs> [interval]"""
import math
import struct
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iaf  # noqa: E402

g = iaf.Game()
vid, tid, secs = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3])
dt = float(sys.argv[4]) if len(sys.argv) > 4 else 0.5


def rot(q, v):
    x, y, z, w = q
    ux, uy, uz = v
    cx, cy, cz = y * uz - z * uy, z * ux - x * uz, x * uy - y * ux
    ccx, ccy, ccz = y * cz - z * cy, z * cx - x * cz, x * cy - y * cx
    return (ux + 2 * (w * cx + ccx), uy + 2 * (w * cy + ccy), uz + 2 * (w * cz + ccz))


t = g.tile(tid)
isl = g.u32(t + 0x100)
af = g.deque(isl + 0x154)[0]
m = struct.unpack('<12d', g.p.read(af + 0x08, 96))


def local(w):
    return [sum((w[k] - m[9 + k]) * m[3 * r + k] for k in range(3)) for r in range(3)]


def ldir(d):
    return [sum(d[k] * m[3 * r + k] for k in range(3)) for r in range(3)]


env = g.u32(af)
data, cap, head = struct.unpack('<3I', g.p.read(env + 0x180, 12))


def node(i):
    j = (head + i) % cap
    return struct.unpack('<3f', g.p.read(data + 0x40 * j + 0x34, 12))


print('runway start node', node(g.u32(af + 0xb8)), 'end node', node(g.u32(af + 0xb4)))
prev = None
end = time.time() + secs
while time.time() < end:
    v = g.vehicle_ptr(vid)
    if not v:
        print('vehicle gone')
        break
    d0, c0, h0 = struct.unpack('<3I', g.p.read(v + 0x8c, 12))
    body = g.u32(d0 + 4 * (h0 % c0))
    w = struct.unpack('<3d', g.p.read(body + 0x48, 24))
    actor = g.u32(body + 0xf0)
    q = struct.unpack('<4f', g.p.read(actor + 0x50, 16))
    l = local(w)
    nose = ldir(rot(q, (0, 0, 1)))
    vel = ''
    if prev:
        dl = [(l[k] - prev[k]) / dt for k in range(3)]
        vel = 'vel=(%5.1f %5.1f %5.1f) track=%6.1f' % (dl[0], dl[1], dl[2], math.degrees(math.atan2(dl[2], dl[0])))
    prev = l
    tgt = struct.unpack('<3d', g.p.read(v + 0x1d8, 24))
    tl = local(tgt)
    print('%s dock=%d mode=%d state=%d rw=%d  local=(%7.1f %6.1f %7.1f) nose=%6.1f pitch=%5.1f %s tgt=(%7.1f %6.1f %7.1f) tvel=%.1f f=%.2f taxi=%d' % (
        time.strftime('%H:%M:%S'), g.u32(v + 0x174), g.u32(af + 0x74), g.u32(af + 0x78), g.i32(af + 0x7c),
        l[0], l[1], l[2], math.degrees(math.atan2(nose[2], nose[0])), math.degrees(math.asin(max(-1, min(1, nose[1])))),
        vel, tl[0], tl[1], tl[2], g.f32(v + 0x1f8), g.f32(v + 0x1fc), g.p.read(v + 0x200, 1)[0]), flush=True)
    time.sleep(dt)
