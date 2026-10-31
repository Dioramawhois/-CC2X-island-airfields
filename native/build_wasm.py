#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the portable Island Airfields flight-director Wasm module from production C sources."""
import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
DEFAULT_ZIG = REPO.parent / 'Carrier-Command-2-extender/.deps/zig-aarch64-macos-0.15.2/zig'
SOURCES = [HERE / 'plugin/iaf_module.c', HERE / 'plugin/iaf_fd_core.c']
OUT = HERE / 'dist/cc2x_island_airfields_core.wasm'
EXPORTS = ['iaf_module_query', 'iaf_module_request_offset', 'iaf_module_response_offset',
           'iaf_module_create', 'iaf_module_drop', 'iaf_module_reset', 'iaf_module_dispatch']
FLAGS = ['cc', '-target', 'wasm32-wasi', '-mexec-model=reactor', '-std=c11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-fno-strict-aliasing', '-ffp-contract=off', '-s', '-Wl,--no-entry',
         '-Wl,--strip-all', '-Wl,--initial-memory=131072', '-Wl,--max-memory=131072',
         '-Wl,-z,stack-size=32768']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zig', type=Path, default=Path(os.environ.get('CC2X_ZIG', DEFAULT_ZIG)))
    ap.add_argument('--out', type=Path, default=OUT)
    args = ap.parse_args()
    if not args.zig.is_file():
        sys.exit(f'Zig 0.15.2 missing: {args.zig}')
    out = args.out.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.unlink(missing_ok=True)
    cmd = [str(args.zig), *FLAGS, *[f'-Wl,--export={e}' for e in EXPORTS], *map(str, SOURCES), '-lm', '-o', str(out)]
    env = dict(os.environ)
    env.setdefault('ZIG_GLOBAL_CACHE_DIR', str(HERE / '.cache/zig'))
    subprocess.run(cmd, check=True, env=env)
    print(f'{hashlib.sha256(out.read_bytes()).hexdigest()}  {out}')


if __name__ == '__main__':
    main()
