# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned authoring fixtures; no game/Workshop files or third-party Lua execution."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, contents):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(contents)


class ScreenProfileGenerationTest(unittest.TestCase):
    def setUp(self):
        self.generator = load('owned_screen_profile_generator', ROOT/'compatibility/generate_screen_profiles.py')
        self.tools = load('owned_screen_profile_tools', ROOT/'compatibility/revolution_1_6/profile_tools.py')
        self.temporary = tempfile.TemporaryDirectory(prefix='iaf-profile-generation-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def fixtures(self):
        shared = b'<owned-data value="preserve"/>\n'
        old = {'content/scripts/owned.lua': b'return "old owned fixture"\n',
               'content/data/owned.xml': shared}
        new = {**old, 'content/scripts/owned.lua': b'return "new owned fixture"\n'}
        roots, reviews = [], []
        for name, resources in [('owned-19', old), ('owned-20', new)]:
            root = self.root/name
            metadata = ('<data name="'+name+'"/>\n').encode()
            write(root/'mod.xml', metadata)
            for resource, contents in resources.items():
                write(root/resource, contents)
            roots.append(root)
            reviews.append({'name': name, 'workshop_id': 'owned-fixture',
                            'mod_xml_sha256': sha(metadata),
                            'resources_sha256': {key: sha(value) for key, value in resources.items()}})
        return roots, reviews, [old, new]

    def test_reviewed_versions_follow_review_order_and_preserve_full_resources(self):
        roots, reviews, resources = self.fixtures()
        original_reviews = copy.deepcopy(reviews)
        actual = self.generator.reviewed_revolutions(list(reversed(roots)), reviews, self.tools)
        self.assertEqual(actual, [(reviews[0], roots[0], resources[0]),
                                  (reviews[1], roots[1], resources[1])])
        self.assertEqual(reviews, original_reviews)

    def test_every_reviewed_version_is_required_exactly_once(self):
        roots, reviews, _ = self.fixtures()
        for supplied in ([roots[0]], [roots[1]], roots+[roots[0]], []):
            with self.subTest(supplied=supplied), self.assertRaises(ValueError):
                self.generator.reviewed_revolutions(supplied, reviews, self.tools)

    def test_unknown_version_is_rejected_even_with_a_reviewed_screen(self):
        roots, reviews, _ = self.fixtures()
        unknown = self.root/'unknown'
        write(unknown/'mod.xml', b'<data name="unreviewed"/>\n')
        write(unknown/'content/scripts/owned.lua', (roots[0]/'content/scripts/owned.lua').read_bytes())
        write(unknown/'content/data/owned.xml', b'<owned-data value="different"/>\n')
        with self.assertRaises(ValueError):
            self.generator.reviewed_revolutions(roots+[unknown], reviews, self.tools)

    def test_unrelated_changed_or_new_resource_and_missing_resource_reject_version(self):
        roots, reviews, resources = self.fixtures()
        unrelated = roots[0]/'content/data/owned.xml'
        unrelated.write_bytes(b'<owned-data value="unreviewed"/>\n')
        with self.assertRaises(ValueError):
            self.generator.reviewed_revolutions(roots, reviews, self.tools)
        unrelated.write_bytes(resources[0]['content/data/owned.xml'])
        extra = roots[0]/'content/data/extra.bin'
        extra.write_bytes(b'owned extra resource')
        with self.assertRaises(ValueError):
            self.generator.reviewed_revolutions(roots, reviews, self.tools)
        extra.unlink()
        unrelated.unlink()
        with self.assertRaises(ValueError):
            self.generator.reviewed_revolutions(roots, reviews, self.tools)

    def test_mod_metadata_is_pinned_independently_of_unchanged_resources(self):
        roots, reviews, _ = self.fixtures()
        (roots[1]/'mod.xml').write_bytes(b'<data name="unreviewed metadata"/>\n')
        with self.assertRaises(ValueError):
            self.generator.reviewed_revolutions(roots, reviews, self.tools)

    def test_duplicate_review_identity_is_ambiguous(self):
        roots, reviews, _ = self.fixtures()
        with self.assertRaises(ValueError):
            self.generator.reviewed_revolutions(roots, reviews+[copy.deepcopy(reviews[0])], self.tools)

    def test_unique_profiles_preserves_order_and_deduplicates_identical_edits(self):
        first = {'sha256': sha(b'owned first'),
                 'edits': [{'offset': 1, 'remove': 0, 'insert': '-- owned\n'}]}
        second = {'sha256': sha(b'owned second'),
                  'edits': [{'offset': 4, 'remove': 2, 'insert': 'replacement'}]}
        items = [first, second, copy.deepcopy(first)]
        before = copy.deepcopy(items)
        self.assertEqual(self.generator.unique_profiles(items), [first, second])
        self.assertEqual(items, before)

    def test_same_hash_with_different_edits_is_rejected(self):
        first = {'sha256': sha(b'owned source'),
                 'edits': [{'offset': 1, 'remove': 0, 'insert': '-- owned\n'}]}
        for change in ({'offset': 2}, {'remove': 1}, {'insert': '-- different\n'}):
            different = copy.deepcopy(first)
            different['edits'][0].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.generator.unique_profiles([first, different])

    def test_inline_owned_guard_ships_only_the_inserted_expression(self):
        original = 'local label = "\u03a9"\nif vehicle:get() then\n    draw()\nend\n'.encode()
        guard = b' and not mm_is_parked_at_airfield(vehicle)'
        modified = original.replace(b'if vehicle:get() then', b'if vehicle:get()'+guard+b' then')
        profile = self.generator.profile(original, modified)
        self.assertEqual(len(profile['edits']), 1)
        edit = profile['edits'][0]
        self.assertEqual(edit['remove'], 0)
        self.assertEqual(edit['insert'].strip(), guard.decode().strip())
        self.assertEqual(original[:edit['offset']]+edit['insert'].encode()+original[edit['offset']:], modified)

    def test_minimal_edit_preserves_complete_utf8_characters(self):
        original, modified = 'local x = "\u03a9"\n'.encode(), 'local x = "\u03a8"\n'.encode()
        edit, = self.generator.profile(original, modified)['edits']
        self.assertEqual(original[:edit['offset']]+edit['insert'].encode()+
                         original[edit['offset']+edit['remove']:], modified)

    def test_shipped_profiles_retain_reviewed_sources_and_only_insert_map_hooks(self):
        # Retain every reviewed source identity. All map order call sites now
        # remain original; only owned drawing/parking guards are inserted.
        expected = {
            'scripts/screen_holomap.lua': {
                '22b31c3892623275e3ea4acfc1b390eac764a7de0a804a6eba0accc9a9173042',
                'ed7f640a1fc877e4118a6b7df86760556762b587ff80206736b0540cc97f02e8'},
            'scripts/screen_vehicle_control.lua': {
                '5861884dcb3b2fd8e9a469f7cc7f679193d082d97387b771b5071438d1a6a854',
                'dc5f291aaf304c4709dc9a931812f0d1b058834ddb5677dc87467a9c2ea71266',
                'ad7fab1acfdb0c9c3f33fbd32daf9ae1a1c3d5e4920fa2453cf2e48dcc76e95f'},
            'scripts/screen_propulsion.lua': {
                '7382748796166450a599b711b33da8d6fea231de2e28fb413befc2b159509ba0',
                'd0cf3f7fa32fa990a0cd3bd1ed2bdd33ebc9eedff4af62869541999c4e997fb3'},
        }
        descriptor = json.loads((ROOT/'content/cc2x/screen_extensions.json').read_text(encoding='utf-8'))
        entries = {entry['screen']: entry for entry in descriptor['extensions']}
        self.assertEqual(set(entries), set(expected))
        for screen, digests in expected.items():
            profiles = entries[screen]['profiles']
            self.assertEqual({p['sha256'] for p in profiles}, digests)
            self.assertEqual(len(profiles), len(digests))
            if screen != 'scripts/screen_propulsion.lua':
                for profile in profiles:
                    for edit in profile['edits']:
                        self.assertEqual(edit['remove'], 0, 'original map behavior was replaced')
                        self.assertNotIn('add_waypoint', edit['insert'])
                        self.assertTrue('mm_draw_' in edit['insert'] or
                                        'mm_is_parked_at_airfield(' in edit['insert'])
        old = '5861884dcb3b2fd8e9a469f7cc7f679193d082d97387b771b5071438d1a6a854'
        new = 'ad7fab1acfdb0c9c3f33fbd32daf9ae1a1c3d5e4920fa2453cf2e48dcc76e95f'
        control = {item['sha256']: item for item in entries['scripts/screen_vehicle_control.lua']['profiles']}
        for digest, profile in control.items():
            guards = sum(edit['insert'].count('mm_is_parked_at_airfield(') for edit in profile['edits'])
            self.assertEqual(guards, 3 if digest.startswith('dc5f291a') else 6)
        self.assertIn(new, control)
        self.assertNotEqual(control[new]['edits'], control[old]['edits'])

    def test_new_version_offsets_are_rederived_in_utf8_bytes_not_hash_replaced(self):
        # A real reviewed patch can still apply after independent upstream code
        # moves its anchor. Runtime byte offsets must be regenerated from that
        # new source, rather than retained behind its freshly pinned hash.
        name = 'content/scripts/owned.lua'
        baseline = b'-- owned baseline prefix\nlocal baseline = true\n\n'
        canonical_old = baseline+b'function update()\n    original()\nend\n'
        old = b'\xef\xbb\xbf'+canonical_old.replace(b'\n', b'\r\n')
        prefix = '-- owned upstream addition: \u03a9\u754c\nlocal upstream = true\n\n'.encode()
        new = prefix+canonical_old
        patch = (b'--- a/content/scripts/owned.lua\n+++ b/content/scripts/owned.lua\n'
                 b'@@ -4,3 +4,4 @@\n function update()\n+    addon()\n     original()\n end\n')
        expected_old = baseline+b'function update()\n    addon()\n    original()\nend\n'
        expected_new = prefix+expected_old
        modified_old = self.tools.apply_patch(name, old, patch)
        modified_new = self.tools.apply_patch(name, new, patch)
        self.assertEqual(modified_old, expected_old)
        self.assertEqual(modified_new, expected_new)
        old_profile = self.generator.profile(old, modified_old)
        new_profile = self.generator.profile(new, modified_new)

        def replay(source, profile):
            source = self.generator.canonical(source)
            self.assertEqual(sha(source), profile['sha256'])
            result, cursor = [], 0
            for edit in profile['edits']:
                offset = edit['offset']
                self.assertGreaterEqual(offset, cursor)
                self.assertLessEqual(offset+edit['remove'], len(source))
                result.extend((source[cursor:offset], edit['insert'].encode()))
                cursor = offset+edit['remove']
            return b''.join(result)+source[cursor:]

        self.assertEqual(replay(old, old_profile), expected_old)
        self.assertEqual(replay(new, new_profile), expected_new)
        self.assertEqual(new_profile['edits'][0]['offset']-old_profile['edits'][0]['offset'], len(prefix))
        merely_repinned = {**old_profile, 'sha256': new_profile['sha256']}
        self.assertNotEqual(replay(new, merely_repinned), expected_new)


if __name__ == '__main__':
    unittest.main()
