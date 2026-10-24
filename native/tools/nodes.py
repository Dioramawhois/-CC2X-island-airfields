# SPDX-License-Identifier: GPL-3.0-or-later
"""Dump an airfield's environment nodes (taxi graph) in airfield-local coordinates, with raw fields.
usage: nodes.py <tile>"""
import struct
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iaf  # noqa: E402

g = iaf.Game()
tid = int(sys.argv[1])
t = g.tile(tid)
isl = g.u32(t + 0x100)
af = g.deque(isl + 0x154)[0]
env = g.u32(af)
m = struct.unpack('<12d', g.p.read(af + 0x08, 96))
print('airfield 0x%x env 0x%x origin (%.1f %.1f %.1f) x-axis (%.3f %.3f %.3f)' % (af, env, m[9], m[10], m[11], m[0], m[1], m[2]))
print('start node %d end node %d selected %d' % (g.u32(af + 0xb8), g.u32(af + 0xb4), g.u32(af + 0x70)))
data, cap, head, count = struct.unpack('<4I', g.p.read(env + 0x180, 16))
print('nodes: %d (env base offset 0x%x deque)' % (count, 0x180))
for i in range(count):
    j = (head + i) % cap
    raw = g.p.read(data + 0x40 * j, 0x40)
    pos = struct.unpack_from('<3f', raw, 0x34)
    ints = struct.unpack_from('<13I', raw, 0)
    print('%2d pos=(%7.1f %5.1f %7.1f)  %s' % (i, pos[0], pos[1], pos[2], ' '.join('%08x' % v for v in ints)))
# other deques near the node list in env
for off in range(0x140, 0x1e0, 0x10):
    d, c, h, n = struct.unpack('<4I', g.p.read(env + off, 16))
    print('env+0x%03x: data=%08x cap=%d head=%d count=%d' % (off, d, c, h, n))
