# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned synthetic fixtures; never execute Revolution Lua or a game DLL."""
import hashlib
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('iaf_rev_compat', ROOT/'compatibility/revolution_1_6/package.py')

class RevolutionCompatibilityTest(unittest.TestCase):
    def setUp(self):
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)

    def test_changed_or_new_revolution_resource_requires_review(self):
        source = {'content/scripts/example.lua': b'return 1\n'}
        profile = {'resources_sha256': {name: hashlib.sha256(data).hexdigest()
                                       for name, data in source.items()}}
        self.module.verify_profile(source, profile)
        with self.assertRaises(ValueError):
            self.module.verify_profile({**source, 'content/plugin.dll': b'MZ'}, profile)
        with self.assertRaises(ValueError):
            self.module.verify_profile({'content/scripts/example.lua': b'return 2\n'}, profile)
        with self.assertRaises(ValueError):
            self.module.verify_profile({}, profile)

    def test_reviewed_git_patch_preserves_independent_code_and_rejects_mismatch(self):
        original = b'function original()\n    return 1\nend\n'
        patch = b'--- a/content/scripts/example.lua\n+++ b/content/scripts/example.lua\n@@ -1,3 +1,4 @@\n function original()\n+    compatibility_callback()\n     return 1\n end\n'
        output = self.module.apply_patch('content/scripts/example.lua', original, patch)
        self.assertEqual(output, b'function original()\n    compatibility_callback()\n    return 1\nend\n')
        with self.assertRaises(ValueError):
            self.module.apply_patch('content/scripts/example.lua', original.replace(b'return 1', b'return 7'), patch)

    def test_patch_cannot_target_another_file(self):
        with self.assertRaises(ValueError):
            self.module.apply_patch('content/scripts/example.lua', b'return 1\n',
                b'--- a/elsewhere\n+++ b/elsewhere\n@@ -1 +1 @@\n-return 1\n+return 2\n')
