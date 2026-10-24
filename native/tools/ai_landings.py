# SPDX-License-Identifier: GPL-3.0-or-later
"""Watch AI aircraft that land on (or take off from) island airfields, to compare with ours.
Prints a line whenever an AI air vehicle is in docking / docking_taxi / undocking or is the
runway vehicle of an AI airfield. usage: ai_landings.py <seconds> [interval]"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iaf  # noqa: E402

AIR = {8, 10, 12, 14}


def main():
    g = iaf.Game()
    end = time.time() + float(sys.argv[1])
    interval = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
    while time.time() < end:
        srv = g.server()
        if srv:
            runway = {}
            for t in g.deque(srv + 0x440):
                isl = g.u32(t + 0x100) if t else 0
                if isl and g.u32(t + 0x60) != 1:
                    for i, af in enumerate(g.deque(isl + 0x154)):
                        rv = g.i32(af + 0x7c)
                        if rv > 0:
                            runway[rv] = (g.u32(t), g.u32(af + 0x74), g.u32(af + 0x78))
            for v in g.deque(srv + 0x37c):
                if not v or g.u32(v + 0x84) not in AIR or g.u32(v + 0x108) == 1:
                    continue
                vid, dock = g.u32(v + 0x80), g.u32(v + 0x174)
                if dock in (1, 3, 5, 6) or vid in runway:
                    bd, bc, bh = struct.unpack('<3I', g.p.read(v + 0x8c, 12))
                    pos = struct.unpack('<3d', g.p.read(g.u32(bd + 4 * (bh % bc)) + 0x48, 24)) if bd and bc else (0, 0, 0)
                    tgt = struct.unpack('<3d', g.p.read(v + 0x1d8, 24))
                    print('%s AI id=%d def=%d team=%d %-13s runway=%s hp=%d pos=(%.0f, %.0f, %.0f) target=(%.0f, %.0f, %.0f) tvel=%.1f taxi=%d' % (
                        time.strftime('%H:%M:%S'), vid, g.u32(v + 0x84), g.u32(v + 0x108), iaf.DOCK.get(dock, dock),
                        runway.get(vid), g.u32(v + 0x180), *pos, *tgt, g.f32(v + 0x1f8), g.p.read(v + 0x200, 1)[0]), flush=True)
        time.sleep(interval)


if __name__ == '__main__':
    main()
