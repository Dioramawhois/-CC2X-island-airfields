# SPDX-License-Identifier: GPL-3.0-or-later
"""Overview of the loaded game: own air vehicles, carriers, own islands with airfields.
usage: world.py [team]   (default team 1)"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iaf  # noqa: E402

AIR = {8: 'wing_light', 10: 'wing_heavy', 12: 'rotor_light', 14: 'rotor_heavy'}


def main():
    g = iaf.Game()
    team = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    srv = g.server()
    print('server 0x%x' % srv)
    for v in g.deque(srv + 0x37c):
        if not v or g.u32(v + 0x108) != team:
            continue
        d = g.u32(v + 0x84)
        bd, bc, bh = struct.unpack('<3I', g.p.read(v + 0x8c, 12))
        pos = struct.unpack('<3d', g.p.read(g.u32(bd + 4 * (bh % bc)) + 0x48, 24)) if bd and bc else (0, 0, 0)
        kind = AIR.get(d) or ('carrier' if d == 0 else None)
        if kind:
            print('vehicle %4d %-12s %-14s attached=%d fuel=%.0f hp=%d pos=(%.0f, %.0f, %.0f)' % (
                g.u32(v + 0x80), kind, iaf.DOCK.get(g.u32(v + 0x174), '?'), g.u32(v + 0x10c),
                g.f32(v + 0x120), g.u32(v + 0x180), *pos))
    for t in g.deque(srv + 0x440):
        if not t or g.u32(t + 0x60) != team:
            continue
        isl = g.u32(t + 0x100)
        n = g.u32(isl + 0x154 + 0xc) if isl else None
        print('tile %3d pos=(%.0f, %.0f) loaded=%s airfields=%s saved=%d/%d' % (
            g.u32(t), g.f64(t + 0x48), g.f64(t + 0x58), bool(isl), n, g.u32(t + 0xcc), g.p.read(t + 0xd0, 1)[0]))


if __name__ == '__main__':
    main()
