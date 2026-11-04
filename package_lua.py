# SPDX-License-Identifier: GPL-3.0-or-later
"""Package this mod's separate Lua policy and patched screens. Never installs it."""
import argparse
import importlib.util
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent

if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--sdk', type=Path)
    known, _ = parser.parse_known_args()
    sdk = Path(known.sdk or os.environ.get('CC2X_SDK_DIR') or ROOT.parent / 'cc2x').resolve(strict=True)
    source = sdk / 'tools/package_lua_mod.py'
    spec = importlib.util.spec_from_file_location('cc2x_lua_package', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.main(ROOT)
