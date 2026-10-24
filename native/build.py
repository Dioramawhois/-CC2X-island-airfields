# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the Island Airfields CC2X plugin.

    python native/build.py            -> native/dist/cc2x_island_airfields.dll
    python native/build.py --release  -> also verified release/*-rc1.zip and a source snapshot

Uses a separate CC2X checkout. Default: ../cc2x beside this repository.
Override with --sdk PATH or CC2X_SDK_DIR; override the compiler with --zig PATH.
"""
import argparse
from pathlib import Path
from sdk_support import load_sdk, sdk_path

HERE = Path(__file__).resolve().parent
MOD = HERE.parent

SOURCE = HERE / 'plugin' / 'island_airfields.c'
OUT = HERE / 'dist' / 'cc2x_island_airfields.dll'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--release', action='store_true')
    ap.add_argument('--sdk', type=Path, help='separate CC2X repository')
    ap.add_argument('--zig', type=Path, help='Zig 0.15.2 executable')
    args = ap.parse_args()
    folder = sdk_path(args.sdk)
    sdk = load_sdk(folder)
    if args.zig:
        sdk.ZIG = args.zig.resolve()
    sdk.plugin(SOURCE, OUT)
    if args.release:
        from release import prepare_release
        prepare_release(sdk_dir=folder)


if __name__ == '__main__':
    main()
