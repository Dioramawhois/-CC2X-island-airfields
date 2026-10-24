# SPDX-License-Identifier: GPL-3.0-or-later
"""Wait until the current game run's cc2x.log (after the last plugin start line) matches a pattern
`count` times, then print that run's plugin events. usage: wait_log.py <regex> <count> <timeout_s>"""
import re
import sys
import time

LOG = r'C:\Program Files (x86)\Steam\steamapps\common\Carrier Command 2\cc2x.log'
pat, count, timeout = re.compile(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3])


def run_lines():
    lines = open(LOG, encoding='utf-8', errors='replace').read().splitlines()
    start = max((i for i, l in enumerate(lines) if 'Island Airfields v' in l and 'active' in l), default=0)
    return lines[start:]


end = time.time() + timeout
while time.time() < end:
    if sum(1 for l in run_lines() if pat.search(l)) >= count:
        break
    time.sleep(2)
else:
    print('TIMEOUT')
for l in run_lines():
    if 'island_airfields] info: vehicle' in l or 'island_airfields] info: tile' in l or 'session' in l:
        print(l[:230])
