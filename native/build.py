# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the Island Airfields CC2X plugin.

    python native/build.py            -> native/dist/cc2x_island_airfields.dll
    (the finished mod folder: python native/package.py)

Uses a separate CC2X checkout. Default: ../cc2x beside this repository.
Override with --sdk PATH or CC2X_SDK_DIR; override the compiler with --zig PATH.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from sdk_support import load_sdk, sdk_path

HERE = Path(__file__).resolve().parent
MOD = HERE.parent

SOURCE = HERE / 'plugin' / 'island_airfields.c'
OUT = HERE / 'dist' / 'cc2x_island_airfields.dll'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sdk', type=Path, help='separate CC2X repository')
    ap.add_argument('--zig', type=Path, help='Zig 0.15.2 executable')
    args = ap.parse_args()
    folder = sdk_path(args.sdk)
    sdk = load_sdk(folder)
    if args.zig:
        sdk.ZIG = args.zig.resolve()
    sdk.plugin(SOURCE, OUT)
    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    sources = [SOURCE, HERE / 'build.py', HERE / 'sdk_support.py']
    report = {'artifact': OUT.name, 'artifact_sha256': sha(OUT),
              'compiler_sha256': sha(sdk.ZIG),
              'sdk_header_sha256': sha(folder / 'include/cc2x.h'),
              'sdk_source_commit': subprocess.check_output(
                  ['git', '-C', str(folder), 'rev-parse', 'HEAD'], text=True).strip(),
              'source_commit': subprocess.check_output(
                  ['git', '-C', str(MOD), 'rev-parse', 'HEAD'], text=True).strip(),
              'source_dirty': bool(subprocess.check_output(
                  ['git', '-C', str(MOD), 'status', '--porcelain']).strip()),
              'source_sha256': {p.relative_to(MOD).as_posix(): sha(p) for p in sources},
              'game_runtime_tested': False}
    OUT.with_suffix('.build.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
