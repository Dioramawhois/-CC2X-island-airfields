# SPDX-License-Identifier: GPL-3.0-or-later
"""Own production order handler with fake API values, never a game/mod DLL or Lua."""
import argparse
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--zig', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='island-airfields-orders-') as folder:
        output = Path(folder) / 'order-sync-test.exe'
        subprocess.run([str(args.zig.resolve()), 'cc', '-target', 'x86-windows-gnu',
                        '-std=c11', '-O1', '-I', str(args.sdk.resolve() / 'include'),
                        str(HERE / 'order_sync_test.c'), '-o', str(output)], check=True)
        subprocess.run([str(output)], check=True, timeout=10)
