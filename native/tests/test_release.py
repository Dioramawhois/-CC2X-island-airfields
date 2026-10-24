# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline release checks. No game process, commands or installed files are changed."""
import configparser
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import types
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'native'))
import release


class ReleaseTests(unittest.TestCase):
    def test_current_candidate_integrity(self):
        candidate = ROOT / 'release/IslandAirfields-v44-rc1.zip'
        with zipfile.ZipFile(candidate) as archive:
            self.assertIsNone(archive.testzip())
            names = archive.namelist()
            self.assertEqual(len(names), len(set(name.casefold() for name in names)))
            manifest = json.loads(archive.read('release-manifest.json'))
            self.assertEqual(manifest['plugin_version'], 44)
            self.assertFalse(manifest['checks']['fresh_game_acceptance_run'])
            self.assertFalse(manifest['checks']['project_license_confirmed'])
            self.assertEqual(set(names), set(manifest['files']) | {'SHA256SUMS.txt', 'release-manifest.json'})
            for name, info in manifest['files'].items():
                data = archive.read(name)
                self.assertEqual(info['size'], len(data))
                self.assertEqual(info['sha256'], release.digest(data))
            for line in archive.read('SHA256SUMS.txt').decode().splitlines():
                digest, name = line.split('  ', 1)
                self.assertEqual(digest, release.digest(archive.read(name)))
            config = configparser.ConfigParser()
            config.read_string(archive.read('game folder/cc2x_island_airfields.ini').decode())
            for key, value in manifest['settings'].items():
                self.assertEqual(config.getint('island_airfields', key), value)
            self.assertEqual(release.plugin_version(archive.read('game folder/cc2x_island_airfields.dll')), 44)
            self.assertFalse(any(name.lower().endswith(('.exe', '.log', '.pdb', '.pyc')) for name in names))
        sources = ROOT / 'release' / manifest['source_archive']['file']
        self.assertEqual(release.digest(sources.read_bytes()), manifest['source_archive']['sha256'])
        with zipfile.ZipFile(sources) as archive:
            self.assertIsNone(archive.testzip())
            self.assertFalse(any(name.startswith('sdk/') for name in archive.namelist()))
            self.assertIn('native/plugin/island_airfields.c', archive.namelist())
            self.assertFalse(any(name.startswith(('research/', '.git/')) or '/.deps/' in name for name in archive.namelist()))
        checksum, filename = candidate.with_suffix('.zip.sha256').read_text().strip().split('  ', 1)
        self.assertEqual(filename, candidate.name)
        self.assertEqual(checksum, release.digest(candidate.read_bytes()))

    def test_pe_rejects_non_dll_and_wrong_architecture(self):
        with self.assertRaises(ValueError):
            release.pe_image(b'not a DLL')
        binary = bytearray((ROOT / 'native/dist/cc2x_island_airfields.dll').read_bytes())
        pe = struct.unpack_from('<I', binary, 0x3c)[0]
        struct.pack_into('<H', binary, pe + 4, 0x8664)
        with self.assertRaises(ValueError):
            release.pe_image(binary)

    def test_archive_determinism_and_unsafe_paths(self):
        base = ROOT / 'native/build/release-tests'
        base.mkdir(parents=True, exist_ok=True)
        folder = Path(tempfile.mkdtemp(dir=base)).resolve()
        try:
            a, b = folder / 'a.zip', folder / 'b.zip'
            release.write_archive(a, {'game folder/a.dll': b'a', 'README.txt': b'docs'})
            release.write_archive(b, {'README.txt': b'docs', 'game folder/a.dll': b'a'})
            self.assertEqual(a.read_bytes(), b.read_bytes())
            for payload in ({'../outside': b'x'}, {'C:/outside': b'x'}, {'A': b'x', 'a': b'y'}):
                with self.assertRaises(ValueError):
                    release.write_archive(folder / 'unsafe.zip', payload)
        finally:
            # Validate the absolute recursive-cleanup target before deletion.
            self.assertTrue(folder.is_relative_to(base.resolve()) and folder != base.resolve())
            shutil.rmtree(folder)


class FlightVerdictTests(unittest.TestCase):
    def run_fake_flight(self, success):
        # Exercise the real test driver with a fake game/time/log. Never attach to CC2.
        game = types.SimpleNamespace(command=lambda *args: True, vehicle_ptr=lambda vid: 1, u32=lambda addr: 4, game_dir=ROOT)
        spec = importlib.util.spec_from_file_location('fd_test_for_release', ROOT / 'native/tools/fd_test.py')
        module = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'iaf': types.SimpleNamespace(Game=lambda: game)}):
            spec.loader.exec_module(module)
        aircraft = [{'vid': 1, 'kind': 'plane', 'tile': 49, 'tx': 0, 'ty': 0, 'parked': False}]
        clock = iter(range(0, 100000, 1000))
        lines = ['[island_airfields] info: vehicle 1: LANDED (plane) go-arounds 0\n'] if success else []
        log = mock.Mock()
        log.readlines.side_effect = [lines, [], [], [], []]
        with mock.patch.object(module, 'test_aircraft', return_value=aircraft), \
             mock.patch.object(module, 'time', types.SimpleNamespace(time=lambda: next(clock), sleep=lambda _: None)), \
             mock.patch('builtins.open', return_value=log), mock.patch.object(sys, 'argv', ['fd_test.py']), \
             contextlib.redirect_stdout(io.StringIO()):
            return module.main()

    def test_failed_flight_returns_failure(self):
        self.assertEqual(self.run_fake_flight(False), 1)

    def test_successful_flight_returns_success(self):
        self.assertEqual(self.run_fake_flight(True), 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
