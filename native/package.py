# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the finished mod folder - exactly what the Steam Workshop item contains.

    python native/package.py                  -> dist/Island Airfields/
    python native/package.py --install        -> also copies it to %APPDATA%/Carrier Command 2/mods/

dist/Island Airfields/
    mod.xml, thumbnail.png
    content/scripts/*.lua          the game's screens + our changes (lua/build_lua.py)
    content/cc2x/cc2x_island_airfields.dll   the plugin; CC2X 0.4+ loads it when the mod is enabled
                                   (inside content: the game's Workshop upload takes only content)
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAME = 'Island Airfields'


def run(*args):
    subprocess.run([sys.executable, *[str(a) for a in args]], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--install', action='store_true', help='copy into the local mods folder for testing')
    ap.add_argument('--game', help='game folder (for the game scripts the Lua patches apply to)')
    a = ap.parse_args()
    run(ROOT / 'lua' / 'build_lua.py', *(['--game', a.game] if a.game else []))
    run(ROOT / 'native' / 'build.py')
    out = ROOT / 'dist' / NAME
    if out.exists():
        shutil.rmtree(out)
    (out / 'content' / 'scripts').mkdir(parents=True)
    (out / 'content' / 'cc2x').mkdir()
    shutil.copy2(ROOT / 'mod.xml', out / 'mod.xml')
    shutil.copy2(ROOT / 'thumbnail.png', out / 'thumbnail.png')
    for lua in sorted((ROOT / 'content' / 'scripts').glob('*.lua')):
        shutil.copy2(lua, out / 'content' / 'scripts' / lua.name)
    shutil.copy2(ROOT / 'native' / 'dist' / 'cc2x_island_airfields.dll', out / 'content' / 'cc2x' / 'cc2x_island_airfields.dll')
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
