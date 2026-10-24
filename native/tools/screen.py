# SPDX-License-Identifier: GPL-3.0-or-later
"""Screenshot / input helper for live testing.

usage:
  screen.py shot <out.png> [scale]        capture the primary screen (scaled down, default 0.5)
  screen.py click <x> <y>                 left click at screen coordinates (full resolution)
  screen.py move <x> <y>
  screen.py key <vk_hex> [hold_ms]        press a virtual key (e.g. 0x1B = Esc, 0x0D = Enter)
  screen.py focus                          bring the game window to the foreground
"""

import ctypes
import ctypes.wintypes as wt
import struct
import sys
import time
import zlib

user32 = ctypes.WinDLL('user32', use_last_error=True)
gdi32 = ctypes.WinDLL('gdi32', use_last_error=True)

user32.SetProcessDPIAware()


def capture(scale=0.5, region=None):
    w = user32.GetSystemMetrics(0)
    h = user32.GetSystemMetrics(1)
    sx, sy, sw, sh = region if region else (0, 0, w, h)
    hdc = user32.GetDC(None)
    mdc = gdi32.CreateCompatibleDC(hdc)
    ow, oh = int(sw * scale), int(sh * scale)
    bmp = gdi32.CreateCompatibleBitmap(hdc, ow, oh)
    gdi32.SelectObject(mdc, bmp)
    gdi32.SetStretchBltMode(mdc, 4)  # HALFTONE
    gdi32.StretchBlt(mdc, 0, 0, ow, oh, hdc, sx, sy, sw, sh, 0x00CC0020)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [('biSize', wt.DWORD), ('biWidth', wt.LONG), ('biHeight', wt.LONG),
                    ('biPlanes', wt.WORD), ('biBitCount', wt.WORD), ('biCompression', wt.DWORD),
                    ('biSizeImage', wt.DWORD), ('biXPelsPerMeter', wt.LONG), ('biYPelsPerMeter', wt.LONG),
                    ('biClrUsed', wt.DWORD), ('biClrImportant', wt.DWORD)]

    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(bi)
    bi.biWidth = ow
    bi.biHeight = -oh
    bi.biPlanes = 1
    bi.biBitCount = 24
    stride = (ow * 3 + 3) & ~3
    buf = ctypes.create_string_buffer(stride * oh)
    gdi32.GetDIBits(mdc, bmp, 0, oh, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(None, hdc)
    raw = bytearray()
    for y in range(oh):
        row = buf.raw[y * stride: y * stride + ow * 3]
        raw.append(0)
        # BGR -> RGB
        rgb = bytearray(len(row))
        rgb[0::3] = row[2::3]
        rgb[1::3] = row[1::3]
        rgb[2::3] = row[0::3]
        raw += rgb

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', ow, oh, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b'')
    return png, w, h


def click(x, y):
    user32.SetCursorPos(int(x), int(y))
    time.sleep(0.05)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.06)
    user32.mouse_event(0x0004, 0, 0, 0, 0)


def key(vk, hold_ms=60):
    sc = user32.MapVirtualKeyW(vk, 0)
    user32.keybd_event(vk, sc, 0, 0)
    time.sleep(hold_ms / 1000.0)
    user32.keybd_event(vk, sc, 2, 0)


def game_pid():
    import subprocess
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq carrier_command.exe', '/FO', 'CSV', '/NH'],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[0].lower() == 'carrier_command.exe':
            return int(parts[1])
    return None


def game_window():
    pid = game_pid()
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def enum(hwnd, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if pid and p.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(enum, 0)
    return found[0] if found else None


def focus():
    h = game_window()
    if not h:
        return 0
    user32.ShowWindow(h, 9)
    # Alt tap lets SetForegroundWindow succeed from a background process
    user32.keybd_event(0x12, 0, 0, 0)
    user32.SetForegroundWindow(h)
    user32.keybd_event(0x12, 0, 2, 0)
    time.sleep(0.3)
    return 1 if user32.GetForegroundWindow() == h else -1


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'shot':
        scale = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
        region = tuple(int(v) for v in sys.argv[4:8]) if len(sys.argv) >= 8 else None
        png, w, h = capture(scale, region)
        open(sys.argv[2], 'wb').write(png)
        print('screen %dx%d saved' % (w, h))
    elif cmd in ('click', 'move', 'key', 'rel', 'lclick', 'wheel'):
        # never send input unless the game window is in the foreground
        h = game_window()
        if not h or user32.GetForegroundWindow() != h:
            print('game window is not in the foreground; input not sent')
            sys.exit(3)
        if cmd == 'click':
            click(int(sys.argv[2]), int(sys.argv[3]))
        elif cmd == 'rel':
            # relative mouse motion in small steps (games reading raw input rotate the camera)
            dx, dy = int(sys.argv[2]), int(sys.argv[3])
            steps = max(1, max(abs(dx), abs(dy)) // 10)
            for i in range(steps):
                user32.mouse_event(0x0001, dx // steps, dy // steps, 0, 0)
                time.sleep(0.01)
        elif cmd == 'wheel':
            # wheel notches: positive = up, negative = down
            for i in range(abs(int(sys.argv[2]))):
                user32.mouse_event(0x0800, 0, 0, 120 if int(sys.argv[2]) > 0 else -120, 0)
                time.sleep(0.15)
        elif cmd == 'lclick':
            # click at the current cursor position (crosshair interaction)
            user32.mouse_event(0x0002, 0, 0, 0, 0)
            time.sleep(0.08)
            user32.mouse_event(0x0004, 0, 0, 0, 0)
        elif cmd == 'move':
            user32.SetCursorPos(int(sys.argv[2]), int(sys.argv[3]))
        else:
            key(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 60)
    elif cmd == 'click_steam':
        # click only when a Steam window is in the foreground (used to dismiss Steam dialogs)
        fg = user32.GetForegroundWindow()
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(fg, ctypes.byref(p))
        import subprocess
        out = subprocess.run(['tasklist', '/FI', 'PID eq %d' % p.value, '/FO', 'CSV', '/NH'],
                             capture_output=True, text=True).stdout.lower()
        if 'steam' not in out:
            print('foreground is not Steam:', out.strip())
            sys.exit(3)
        click(int(sys.argv[2]), int(sys.argv[3]))
    elif cmd == 'focus':
        print('windows:', focus())
    elif cmd == 'pos':
        pt = wt.POINT()
        user32.GetCursorPos(ctypes.byref(pt))
        h = game_window()
        r = wt.RECT()
        if h:
            user32.GetWindowRect(h, ctypes.byref(r))
        cr = wt.RECT()
        if h:
            user32.GetClientRect(h, ctypes.byref(cr))
            p0 = wt.POINT(0, 0)
            user32.ClientToScreen(h, ctypes.byref(p0))
        print('cursor', pt.x, pt.y, 'window', r.left, r.top, r.right, r.bottom,
              'client origin', p0.x if h else None, p0.y if h else None, 'client size', cr.right, cr.bottom,
              'foreground', user32.GetForegroundWindow() == h)
