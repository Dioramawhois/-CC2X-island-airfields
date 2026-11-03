# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply owned screen patches to legitimate local SDK scripts, entirely in memory."""
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent

def resources(game, read):
    if game is None:
        raise ValueError('Island Airfields packaging requires --game with the official Mod SDK scripts')
    generation_rule = read(ROOT, 'integrations/owned-island-generation.json')
    generation = json.loads(generation_rule)
    if hashlib.sha256(read(game, 'carrier_command.exe')).hexdigest() != generation['exe_sha256']:
        raise ValueError('Own-island generation metadata requires the reviewed client executable')
    spec = importlib.util.spec_from_file_location('airfield_owned_patch', ROOT / 'lua/build_lua.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result, sources = {}, {}
    for patch in sorted((ROOT / 'lua').glob('*.lua.patch')):
        name = patch.name.removesuffix('.patch')
        original = read(game, 'mod_dev_kit/source/scripts/' + name)
        delta = read(ROOT, 'lua/' + patch.name)
        lines = original.decode('utf-8-sig').replace('\r\n', '\n').split('\n')
        edits = delta.decode('utf-8').splitlines()
        merged = module.apply_patch(lines, edits, name)
        target = 'content/scripts/' + name
        generated = '\n'.join(merged).encode('utf-8')
        cached = ROOT / target
        if cached.exists() or cached.is_symlink():
            if read(ROOT, target) != generated:
                raise ValueError('Cached screen differs from its current patch: ' + target)
            # The SDK collector already included this byte-identical cache.
        else:
            result[target] = generated
        sources[name] = {'base_sha256': hashlib.sha256(original).hexdigest(),
                         'patch_sha256': hashlib.sha256(delta).hexdigest()}
    return result, {'screen_inputs_sha256': sources,
                    'island_generation_profile': generation['profile'],
                    'island_generation_exe_sha256': generation['exe_sha256'],
                    'island_generation_rule_sha256': hashlib.sha256(generation_rule).hexdigest(),
                    'patch_engine_sha256': hashlib.sha256(read(ROOT, 'lua/build_lua.py')).hexdigest()}
