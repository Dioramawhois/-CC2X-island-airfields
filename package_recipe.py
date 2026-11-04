# SPDX-License-Identifier: GPL-3.0-or-later
"""Package independent gameplay and source-free screen declarations only."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent

def resources(game, read):
    if game is None:
        raise ValueError('Island Airfields packaging requires --game for the reviewed client profile')
    generation_rule = read(ROOT, 'integrations/owned-island-generation.json')
    generation = json.loads(generation_rule)
    if hashlib.sha256(read(game, 'carrier_command.exe')).hexdigest() != generation['exe_sha256']:
        raise ValueError('Own-island generation metadata requires the reviewed client executable')
    cached = ROOT / 'content/scripts'
    if cached.exists() or cached.is_symlink():
        raise ValueError('Whole screen caches are forbidden in the separate addon')
    descriptor = read(ROOT, 'content/cc2x/screen_extensions.json')
    declaration = json.loads(descriptor)
    if declaration.get('schema') != 2 or not declaration.get('extensions'):
        raise ValueError('Hash-pinned screen extension profiles are required')
    return {}, {'screen_delivery': 'sdk-hash-pinned-source-edits-v1',
                    'original_screens_bundled': False,
                    'screen_descriptor_sha256': hashlib.sha256(descriptor).hexdigest(),
                    'island_generation_profile': generation['profile'],
                    'island_generation_exe_sha256': generation['exe_sha256'],
                    'island_generation_rule_sha256': hashlib.sha256(generation_rule).hexdigest(),
                    'required_runtime_capability': 'screen-source-edits-v1'}
