# SPDX-License-Identifier: GPL-3.0-or-later
"""Own production order handler with fake API values, never a game/mod DLL or Lua."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--zig', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--report', type=Path, help='write hashed offline evidence, never gameplay acceptance')
    args = parser.parse_args()
    inputs = [HERE / 'order_sync_test.c', HERE / 'run_order_test.py',
              HERE.parent / 'plugin/island_airfields.c']
    headers = [args.sdk.resolve() / 'include' / name for name in
               ('cc2x.h', 'cc2x_session.h', 'cc2x_session_win32.h')]
    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    hashes = {str(path.resolve()): sha(path) for path in inputs + headers}
    report = {'status': 'running', 'game_runtime_tested': False,
              'remote_multiplayer_tested': False, 'dedicated_server_tested': False,
              'assertions_enabled': True, 'source_sha256': hashes,
              'compiler_sha256': sha(args.zig.resolve())}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='island-airfields-orders-') as folder:
        output = Path(folder) / 'order-sync-test.exe'
        subprocess.run([str(args.zig.resolve()), 'cc', '-target', 'x86-windows-gnu',
                        '-std=c11', '-O1', '-UNDEBUG', '-I', str(args.sdk.resolve() / 'include'),
                        str(HERE / 'order_sync_test.c'), '-o', str(output)], check=True)
        subprocess.run([str(output)], check=True, timeout=10)
        report['test_executable_sha256'] = sha(output)
    if hashes != {str(path.resolve()): sha(path) for path in inputs + headers}:
        raise RuntimeError('Regression inputs changed during verification')
    report['status'] = 'passed'
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
