# SPDX-License-Identifier: GPL-3.0-or-later
"""Cancel test: takeoff, landing order, a new order during the approach (must cancel), landing again.
usage: cancel_test.py <vid> <tile> <tile_x> <tile_y> <cancel_on_regex>"""
import re
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iaf  # noqa: E402

LOG = r'C:\Program Files (x86)\Steam\steamapps\common\Carrier Command 2\cc2x.log'
vid, tile, tx, ty, cancel_on = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
g = iaf.Game()
log = open(LOG, encoding='utf-8', errors='replace')
log.seek(0, 2)


def wait(pattern, timeout):
    end = time.time() + timeout
    while time.time() < end:
        for line in log.readlines():
            m = re.search(r'info: vehicle %d: (.*)' % vid, line)
            if m:
                print('  %s' % m.group(1).strip(), flush=True)
                if re.search(pattern, m.group(1)):
                    return m.group(1)
        time.sleep(0.5)
    return None


print('takeoff', g.command(1, vid, 0, tx + 2500, ty + 2500))
if not wait(r'TOOK OFF', 120):
    raise SystemExit('FAIL: no takeoff')
time.sleep(15)
print('landing order', g.command(2, vid, tile, tx, ty))
if not wait(cancel_on, 240):
    raise SystemExit('FAIL: never reached ' + cancel_on)
time.sleep(2)
print('new order (should cancel)', g.command(1, vid, 0, tx - 2500, ty + 2500))
ok_cancel = wait(r'landing cancelled', 15)
print('CANCEL', 'PASS' if ok_cancel else 'FAIL')
v = g.vehicle(vid)
print(' ', v)
time.sleep(10)
print('landing order again', g.command(2, vid, tile, tx, ty))
r = wait(r'LANDED|ABORTED|LOST', 420)
print('RELAND', 'PASS' if r and r.startswith('LANDED') else 'FAIL', r)
