# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('airfields_recipe', ROOT / 'package_recipe.py')

def read(root, name):
    return (root / name).read_bytes()

class RecipeTest(unittest.TestCase):
    def setUp(self):
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name) / 'mod'
        self.game = Path(self.temp.name) / 'game'
        (self.project / 'lua').mkdir(parents=True)
        (self.project / 'integrations').mkdir()
        (self.game).mkdir()
        (self.game / 'carrier_command.exe').write_bytes(b'owned synthetic client fixture')
        (self.project / 'integrations/owned-island-generation.json').write_text(json.dumps({
            'profile': 'synthetic-engine',
            'exe_sha256': hashlib.sha256(b'owned synthetic client fixture').hexdigest(),
        }))
        (self.game / 'mod_dev_kit/source/scripts').mkdir(parents=True)
        (self.project / 'lua/build_lua.py').write_bytes((ROOT / 'lua/build_lua.py').read_bytes())
        (self.game / 'mod_dev_kit/source/scripts/example.lua').write_bytes(b'return 1\n')
        (self.project / 'lua/example.lua.patch').write_text('--- game/example.lua\n+++ mod/example.lua\n@@ -1 +1 @@\n-return 1\n+return 2\n')
        self.module.ROOT = self.project

    def test_fresh_recipe_preserves_game_and_reports_patch_and_base_hashes(self):
        result, evidence = self.module.resources(self.game, read)
        self.assertEqual(result, {'content/scripts/example.lua': b'return 2\n'})
        self.assertEqual((self.game / 'mod_dev_kit/source/scripts/example.lua').read_bytes(), b'return 1\n')
        self.assertEqual(len(evidence['screen_inputs_sha256']['example.lua']['base_sha256']), 64)

    def test_identical_cache_is_already_collected_and_stale_cache_is_refused(self):
        cached = self.project / 'content/scripts/example.lua'
        cached.parent.mkdir(parents=True)
        cached.write_bytes(b'return 2\n')
        self.assertEqual(self.module.resources(self.game, read)[0], {})
        cached.write_bytes(b'return 3\n')
        with self.assertRaisesRegex(ValueError, 'Cached screen differs'):
            self.module.resources(self.game, read)

    def test_missing_game_is_refused(self):
        with self.assertRaises(ValueError):
            self.module.resources(None, read)

    def test_unreviewed_generation_rule_engine_is_refused(self):
        (self.game / 'carrier_command.exe').write_bytes(b'different synthetic engine')
        with self.assertRaisesRegex(ValueError, 'reviewed client executable'):
            self.module.resources(self.game, read)

if __name__ == '__main__':
    unittest.main()
