# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the Island Airfields CC2X plugin.

    python native/build.py            -> native/dist/cc2x_island_airfields.dll
    (the finished mod folder: python native/package.py)

Uses a separate CC2X checkout. Default: ../cc2x beside this repository.
Override with --sdk PATH or CC2X_SDK_DIR; override the compiler with --zig PATH.
"""
import argparse
from pathlib import Path
from sdk_support import load_sdk, sdk_path

HERE = Path(__file__).resolve().parent
MOD = HERE.parent

WASM3 = HERE / 'third_party' / 'wasm3' / 'source'
# The production core and module wrapper are compiled only into the Wasm asset, never the DLL.
SOURCES = [HERE / 'plugin' / 'island_airfields.c', HERE / 'plugin' / 'wasm_host.c',
           *[WASM3 / (name + '.c') for name in (
               'm3_bind', 'm3_code', 'm3_compile', 'm3_core', 'm3_deterministic', 'm3_env',
               'm3_exec', 'm3_function', 'm3_info', 'm3_module', 'm3_parse', 'm3_validate')]]
OUT = HERE / 'dist' / 'cc2x_island_airfields.dll'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sdk', type=Path, help='separate CC2X repository')
    ap.add_argument('--zig', type=Path, help='Zig 0.15.2 executable')
    args = ap.parse_args()
    OUT.unlink(missing_ok=True)      # a failed build must not leave an older DLL behind
    folder = sdk_path(args.sdk)
    sdk = load_sdk(folder)
    if args.zig:
        sdk.ZIG = args.zig.resolve()
    sdk.CFLAGS.extend(['-I', str(WASM3), '-Dd_m3HasGasMetering=1', '-Dd_m3HasValidation=1'])
    try:
        sdk.plugin(SOURCES, OUT)
    except BaseException:
        OUT.unlink(missing_ok=True)  # also discard a partially written compiler output
        raise


if __name__ == '__main__':
    main()
