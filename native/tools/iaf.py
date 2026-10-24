# SPDX-License-Identifier: GPL-3.0-or-later
"""Island Airfields developer tool (game running with CC2X + cc2x_island_airfields.dll).

usage:
  iaf.py status                               plugin statistics
  iaf.py island <tile_id>                     airfields of an island (runway, queues, hangars)
  iaf.py vehicle <vehicle_id>                 dock state / fuel / position of a vehicle
  iaf.py move <vehicle_id> <x> <y>            order through the Lua map code (vehicle screen must run)
  iaf.py land <vehicle_id> <tile_id> <x> <y>  landing order through the Lua map code
  iaf.py dock <vehicle_id> <carrier_id> <x> <y>
  iaf.py trace <vehicle_id> <tile_id> <seconds> [interval]
"""
import math
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proc as T   # noqa: E402  (process / memory helpers)

PLUGIN_DLL = 'cc2x_island_airfields.dll'
EXPORT = b'cc2x_island_airfields_debug'
STATS = ['land_requests', 'land_queued', 'land_rejected', 'land_pending', 'takeoff_requests',
         'takeoff_queued', 'takeoff_rejected', 'hangar_assigned', 'hangar_cleaned', 'islands_enabled',
         'spawns_skipped', 'table_refresh', 'lua_queries', 'server_ticks', 'glide_fixes', 'taxi_forced',
         'arrest_ticks', 'rollouts', 'land_cancelled']
DOCK = {0: 'undocked', 1: 'docking', 2: 'pending_undock', 3: 'undocking', 4: 'docked', 5: 'dock_queue',
        6: 'docking_taxi', 7: 'undock_holding'}
# Debug struct layout (island_airfields.c)
OFF_STATS, OFF_CMD, OFF_WATCH_ID, OFF_WATCH_PTR, OFF_SERVER, OFF_DBG = 8, 8 + 128, 8 + 128 + 20, 8 + 128 + 24, 8 + 128 + 28, 8 + 128 + 32


class Game:
    def __init__(self):
        pid = T.find_process(T.PROCESS_NAME)
        if not pid:
            raise SystemExit('game not running')
        self.p = T.Process(pid)
        base, _ = T.module_base(pid, PLUGIN_DLL)
        self.dbg = base + self._export_rva(base, EXPORT)
        magic, self.version = struct.unpack('<II', self.p.read(self.dbg, 8))
        if magic != 0x44464149:
            raise SystemExit('unexpected plugin debug block')

    def _export_rva(self, base, name):
        pe = self.u32(base + 0x3c)
        ed = self.u32(base + pe + 0x78)
        n, funcs, names, ords = struct.unpack('<4I', self.p.read(base + ed + 0x18, 16))
        for i in range(n):
            s = self.p.read(base + self.u32(base + names + 4 * i), len(name) + 1)
            if s == name + b'\0':
                o = struct.unpack('<H', self.p.read(base + ords + 2 * i, 2))[0]
                return self.u32(base + funcs + 4 * o)
        raise SystemExit('plugin export not found')

    def u32(self, a): return self.p.read_u32(a)
    def i32(self, a): return struct.unpack('<i', self.p.read(a, 4))[0]
    def f32(self, a): return struct.unpack('<f', self.p.read(a, 4))[0]
    def f64(self, a): return struct.unpack('<d', self.p.read(a, 8))[0]
    def w32(self, a, v): self.p.write(a, struct.pack('<i', v))

    def stats(self):
        return dict(zip(STATS, struct.unpack('<%dI' % len(STATS), self.p.read(self.dbg + OFF_STATS, 4 * len(STATS)))))

    def dbgvals(self):
        return struct.unpack('<8d', self.p.read(self.dbg + OFF_DBG, 64))

    def deque(self, a):
        data, cap, head, count = struct.unpack('<4I', self.p.read(a, 16))
        return [self.u32(data + 4 * ((head + i) % cap)) for i in range(count)] if cap else []

    def server(self):
        return self.u32(self.dbg + OFF_SERVER)

    def tile(self, tid):
        for t in self.deque(self.server() + 0x440):
            if t and self.u32(t) == tid:
                return t
        return 0

    def command(self, cmd, a, b, c, d):
        for i, v in enumerate((a, b, c, d)):
            self.w32(self.dbg + OFF_CMD + 4 + 4 * i, v)
        self.w32(self.dbg + OFF_CMD, cmd)
        time.sleep(0.5)
        return self.u32(self.dbg + OFF_CMD) == 0

    def vehicle_ptr(self, vid):
        # scan the server's vehicle list (several tools may run at once; no shared watch slot)
        srv = self.server()
        if not srv:
            return 0
        for v in self.deque(srv + 0x37c):
            if v and self.u32(v + 0x80) == vid:
                return v
        return 0

    def vehicle(self, vid):
        v = self.vehicle_ptr(vid)
        if not v:
            return 'vehicle %d: not found' % vid
        data, cap, head = struct.unpack('<3I', self.p.read(v + 0x8c, 12))
        pos = (float('nan'),) * 3
        if data and cap:
            b = self.u32(data + 4 * (head % cap))
            if b:
                pos = struct.unpack('<3d', self.p.read(b + 0x48, 24))
        tgt = struct.unpack('<3d', self.p.read(v + 0x1d8, 24))
        return ('vehicle %d: %-14s attached=%d waypoints=%d fuel=%.1f hp=%d ctrl=%d pos=(%.0f, %.0f, %.0f) '
                'target=(%.0f, %.0f, %.0f) tvel=%.1f taxi=%d' % (
                    vid, DOCK.get(self.u32(v + 0x174), '?'), self.u32(v + 0x10c), self.u32(v + 0x14c),
                    self.f32(v + 0x120), self.u32(v + 0x180), self.u32(v + 0x1d0), *pos, *tgt,
                    self.f32(v + 0x1f8), self.p.read(v + 0x200, 1)[0]))

    def island(self, tid):
        t = self.tile(tid)
        if not t:
            return 'tile %d: not found' % tid
        isl = self.u32(t + 0x100)
        out = ['tile %d team=%d island=0x%x saved=%d/%d' % (tid, self.u32(t + 0x60), isl, self.u32(t + 0xcc), self.p.read(t + 0xd0, 1)[0])]
        if isl:
            for i, af in enumerate(self.deque(isl + 0x154)):
                h = [self.u32(self.u32(af + 0x68) + 4 * k) for k in range(self.u32(af + 0x6c))]
                out.append('  airfield %d mode=%d runway_state=%d runway_vehicle=%d dock_queue=%s undock_queue=%s hangars=%s' % (
                    i, self.u32(af + 0x74), self.u32(af + 0x78), self.i32(af + 0x7c),
                    self.deque(af + 0xcc), self.deque(af + 0xdc), h))
        return '\n'.join(out)


def trace(g, vid, tid, secs, interval):
    end, last = time.time() + secs, None
    while time.time() < end:
        v = g.vehicle(vid)
        now = time.time()
        m = re.search(r'pos=\((\S+), (\S+), (\S+)\)', v)
        speed = ''
        if m:
            p = tuple(float(x) for x in m.groups())
            if last:
                speed = ' v=%.0f' % (math.dist(p, last[1]) / (now - last[0]))
            last = (now, p)
        print(time.strftime('%H:%M:%S ') + v.split(': ', 1)[1] + speed, flush=True)
        print('         ' + g.island(tid).split('\n')[-1].strip(), flush=True)
        d = g.dbgvals()
        print('         dbg dist=%.0f lx=%.0f vanilla_y=%.1f target_y=%.1f speed=%.1f height=%.1f taxi_dist=%.1f' % d[:7], flush=True)
        if 'not found' in v:
            break
        time.sleep(interval)


def main():
    g = Game()
    cmd, a = sys.argv[1], [int(float(x)) for x in sys.argv[2:]]
    if cmd == 'status':
        print('Island Airfields v%d, server 0x%x' % (g.version, g.server()))
        for k, v in g.stats().items():
            print('  %-20s %d' % (k, v))
    elif cmd == 'island':
        print(g.island(a[0]))
    elif cmd == 'vehicle':
        print(g.vehicle(a[0]))
    elif cmd in ('move', 'land', 'dock'):
        args = {'move': (1, a[0], 0, a[1], a[2]), 'land': (2, *a[:4]), 'dock': (3, *a[:4])}[cmd]
        print('command consumed' if g.command(*args) else 'command NOT consumed (vehicle screen not running?)')
    elif cmd == 'trace':
        trace(g, a[0], a[1], a[2], float(sys.argv[5]) if len(sys.argv) > 5 else 1.0)
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
