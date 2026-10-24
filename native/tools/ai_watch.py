# SPDX-License-Identifier: GPL-3.0-or-later
"""List AI aircraft that are landing / taxiing / parked on islands (to compare with ours).
usage: ai_watch.py [seconds] [interval]"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iaf  # noqa: E402

AIR = {8: 'wing_light', 10: 'wing_heavy', 12: 'rotor_light', 14: 'rotor_heavy'}


def scan(g):
    srv = g.server()
    rows = []
    for v in g.deque(srv + 0x37c):
        if not v:
            continue
        team_obj = g.u32(v + 0x1a8)
        is_ai = team_obj and g.p.read(team_obj + 0x19, 1)[0]
        dock = g.u32(v + 0x174)
        if not is_ai or dock == 0:
            continue
        bd, bc, bh = struct.unpack('<3I', g.p.read(v + 0x8c, 12))
        pos = struct.unpack('<3d', g.p.read(g.u32(bd + 4 * (bh % bc)) + 0x48, 24)) if bd and bc else (0, 0, 0)
        rows.append((g.u32(v + 0x80), g.u32(v + 0x84), g.u32(v + 0x108), iaf.DOCK.get(dock, dock),
                     g.u32(v + 0x10c), g.u32(v + 0x1d0), g.p.read(v + 0x200, 1)[0], pos))
    return rows


def main():
    g = iaf.Game()
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 0
    interval = float(sys.argv[2]) if len(sys.argv) > 2 else 5
    end = time.time() + secs
    while True:
        for vid, d, team, dock, att, ctrl, taxi, pos in scan(g):
            print('%s id=%d def=%d team=%d %-14s attached=%d ctrl=%d taxi=%d pos=(%.0f, %.0f, %.0f)' % (
                time.strftime('%H:%M:%S'), vid, d, team, dock, att, ctrl, taxi, *pos), flush=True)
        if time.time() >= end:
            break
        time.sleep(interval)


if __name__ == '__main__':
    main()
