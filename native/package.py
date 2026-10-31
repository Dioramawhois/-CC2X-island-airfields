# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the finished mod folder - exactly what the Steam Workshop item contains.

    python native/package.py                  -> dist/Island Airfields/
    python native/package.py --install        -> also copies it to %APPDATA%/Carrier Command 2/mods/

dist/Island Airfields/
    mod.xml, thumbnail.png
    content/scripts/*.lua          the game's screens + our changes (lua/build_lua.py)
    content/cc2x/cc2x_island_airfields.dll   the plugin with its embedded interpreter
    content/cc2x/cc2x_island_airfields_core.wasm   the same portable module on every host
    content/cc2x/wasm3-LICENSE.txt    interpreter license
                                   (inside content: the game's Workshop upload takes only content)
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAME = 'Island Airfields'
ARTIFACTS = ('cc2x_island_airfields.dll', 'cc2x_island_airfields_core.wasm')


def run(*args):
    subprocess.run([sys.executable, *[str(a) for a in args]], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--install', action='store_true', help='copy into the local mods folder for testing')
    ap.add_argument('--game', help='game folder (for the game scripts the Lua patches apply to)')
    ap.add_argument('--sdk', type=Path, help='separate CC2X repository')
    ap.add_argument('--zig', type=Path, help='Zig 0.15.2 executable')
    a = ap.parse_args()
    out = ROOT / 'dist' / NAME
    if out.exists():
        shutil.rmtree(out)           # no old package may survive a failed rebuild
    native_dist = ROOT / 'native' / 'dist'
    for name in ARTIFACTS:
        (native_dist / name).unlink(missing_ok=True)
    run(ROOT / 'lua' / 'build_lua.py', *(['--game', a.game] if a.game else []))
    zig = ['--zig', a.zig] if a.zig else []
    run(ROOT / 'native' / 'build.py', *(['--sdk', a.sdk] if a.sdk else []), *zig)
    run(ROOT / 'native' / 'build_wasm.py', *zig)
    hashes = {name: hashlib.sha256((native_dist / name).read_bytes()).digest() for name in ARTIFACTS}
    (out / 'content' / 'scripts').mkdir(parents=True)
    (out / 'content' / 'cc2x').mkdir()
    shutil.copy2(ROOT / 'mod.xml', out / 'mod.xml')
    shutil.copy2(ROOT / 'thumbnail.png', out / 'thumbnail.png')
    for lua in sorted((ROOT / 'content' / 'scripts').glob('*.lua')):
        shutil.copy2(lua, out / 'content' / 'scripts' / lua.name)
    try:
        for name in ARTIFACTS:
            dest = out / 'content' / 'cc2x' / name
            shutil.copy2(native_dist / name, dest)
            if hashlib.sha256(dest.read_bytes()).digest() != hashes[name]:
                raise ValueError('packaged artifact differs from build: %s' % name)
        license_file = ROOT / 'native' / 'third_party' / 'wasm3' / 'LICENSE'
        shutil.copy2(license_file, out / 'content' / 'cc2x' / 'wasm3-LICENSE.txt')
    except BaseException:
        shutil.rmtree(out)
        raise
    for f in sorted(out.rglob('*')):
        if f.is_file():
            print('  %-50s %8d' % (f.relative_to(out).as_posix(), f.stat().st_size))
    print('mod folder:', out)
    if a.install:
        dest = Path(os.environ['APPDATA']) / 'Carrier Command 2' / 'mods' / NAME
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(out, dest)
        print('installed to', dest, '- enable it in the game: Mods menu')


if __name__ == '__main__':
    main()
