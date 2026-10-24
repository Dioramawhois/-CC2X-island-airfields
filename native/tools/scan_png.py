# SPDX-License-Identifier: GPL-3.0-or-later
"""Render iaf_scan_<tile>.txt (top|low heights, 2 m grid, x -220..140, z -200..100) to a PNG with the
airfield nodes. usage: scan_png.py <scan.txt> <out.png> [low]"""
import struct
import sys
import zlib

src, out = sys.argv[1], sys.argv[2]
use_low = len(sys.argv) > 3
rows = []
for line in open(src):
    if line.startswith('#'):
        continue
    cells = [c.split('|') for c in line.split()]
    rows.append([float(c[1] if use_low else c[0]) for c in cells])
S = 3                                  # pixels per cell
H, W = len(rows), len(rows[0])
x0, z0 = -220.0, -200.0
NODES = [(75, -140), (-150, -140), (0, 0), (0, -130), (-20, -20), (20, -20), (-20, 20), (20, 20), (0, 40), (0, -40),
         (-10, -140), (-120, -140), (-130, -160), (-150, -180), (-170, -160), (0, 20), (0, -20)]


def color(h):
    if h <= -98:
        return (0, 0, 60)               # nothing / sea
    if h < -1.0:
        return (20, 60, 20)
    if h < 0.6:                          # flat ground at the airfield level
        return (120, 120, 120)
    if h < 2.0:
        return (170, 170, 90)
    if h < 4.5:
        return (200, 120, 40)
    if h < 8:
        return (220, 60, 40)
    return (255, 255, 255)


img = [[(0, 0, 0)] * (W * S) for _ in range(H * S)]
for r in range(H):
    for c in range(W):
        col = color(rows[r][c])
        x, z = x0 + 2 * c, z0 + 2 * r
        if x % 20 == 0 or z % 20 == 0:
            col = tuple(min(255, v + 35) for v in col)
        for dy in range(S):
            for dx in range(S):
                img[(H - 1 - r) * S + dy][c * S + dx] = col
for nx, nz in NODES:
    c, r = int((nx - x0) / 2), int((nz - z0) / 2)
    for dy in range(-4, 5):
        for dx in range(-4, 5):
            yy, xx = (H - 1 - r) * S + dy, c * S + dx
            if 0 <= yy < H * S and 0 <= xx < W * S and (abs(dx) == 4 or abs(dy) == 4):
                img[yy][xx] = (0, 255, 255)
raw = bytearray()
for row in img:
    raw.append(0)
    for p in row:
        raw += bytes(p)


def chunk(t, d):
    c = struct.pack('>I', len(d)) + t + d
    return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)


png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W * S, H * S, 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b'')
open(out, 'wb').write(png)
print('ok', W * S, H * S)
