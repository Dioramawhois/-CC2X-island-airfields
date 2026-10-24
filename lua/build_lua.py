# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the mod's Lua screens: the game's own scripts from the official mod dev kit plus our patches.

The game scripts are not ours and are not in this repository; only our changes are (lua/*.patch,
unified diffs). This script applies them to the scripts of the locally installed game and writes
content/scripts/*.lua, which is what the mod folder / Workshop item contains.

usage:  python lua/build_lua.py [--game <Carrier Command 2 folder>]
        python lua/build_lua.py --diff     (after editing content/scripts: rewrite the patches)
"""
import argparse
import difflib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'content', 'scripts')
DEFAULT_GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Carrier Command 2'
HUNK = re.compile(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@')


def read_lines(path):
    with open(path, encoding='utf-8-sig', newline='') as f:
        return f.read().replace('\r\n', '\n').split('\n')


def apply_patch(src, patch_lines, name):
    out, pos, i = [], 0, 0
    while i < len(patch_lines):
        m = HUNK.match(patch_lines[i])
        if not m:
            i += 1
            continue
        start = int(m.group(1)) - (0 if m.group(2) == '0' else 1)
        out.extend(src[pos:start])
        pos = start
        i += 1
        while i < len(patch_lines) and not patch_lines[i].startswith('@@'):
            line = patch_lines[i]
            tag, text = (line[:1], line[1:]) if line else (' ', '')
            if tag in ' -':
                if pos >= len(src) or src[pos] != text:
                    raise SystemExit('%s: the game script changed near line %d - the patch needs updating' % (name, pos + 1))
                pos += 1
                if tag == ' ':
                    out.append(text)
            elif tag == '+':
                out.append(text)
            i += 1
    out.extend(src[pos:])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', default=os.environ.get('CC2_GAME_DIR', DEFAULT_GAME))
    ap.add_argument('--diff', action='store_true', help='rewrite lua/*.patch from content/scripts')
    a = ap.parse_args()
    scripts = os.path.join(a.game, 'mod_dev_kit', 'source', 'scripts')
    if not os.path.isdir(scripts):
        raise SystemExit('game scripts not found: %s (use --game or CC2_GAME_DIR)' % scripts)
    names = sorted(f[:-len('.patch')] for f in os.listdir(HERE) if f.endswith('.lua.patch'))
    if a.diff:
        names = sorted(f for f in os.listdir(OUT) if f.endswith('.lua'))
    os.makedirs(OUT, exist_ok=True)
    for name in names:
        game = read_lines(os.path.join(scripts, name))
        patch_path = os.path.join(HERE, name + '.patch')
        if a.diff:
            ours = read_lines(os.path.join(OUT, name))
            diff = difflib.unified_diff(game, ours, 'game/' + name, 'mod/' + name, n=3, lineterm='')
            with open(patch_path, 'w', encoding='utf-8', newline='\n') as f:
                f.write('\n'.join(diff) + '\n')
            print('patch written:', os.path.relpath(patch_path, ROOT))
        else:
            with open(patch_path, encoding='utf-8') as f:
                patch = f.read().split('\n')
            if patch and patch[-1] == '':
                patch.pop()                              # the file's final newline, not a line
            ours = apply_patch(game, patch, name)
            with open(os.path.join(OUT, name), 'w', encoding='utf-8', newline='\n') as f:
                f.write('\n'.join(ours))
            print('built:', os.path.relpath(os.path.join(OUT, name), ROOT))


if __name__ == '__main__':
    sys.exit(main())
