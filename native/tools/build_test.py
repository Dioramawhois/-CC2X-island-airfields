# SPDX-License-Identifier: GPL-3.0-or-later
"""Build chain test: native/build.py and native/package.py with a fake compiler (no Zig, no game).

    python native/tools/build_test.py [--sdk PATH]     (default: ../cc2x or CC2X_SDK_DIR)

The real CC2X build helper runs; only the compiler is replaced by a script that records its
arguments and writes the -o file (or fails). Output paths are redirected to a temporary folder,
native/dist is not touched.
"""
import importlib.util
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

NATIVE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(NATIVE))
SDK = None

FAKE_ZIG_SH = '#!/bin/sh\nexec "%s" "%s" "$@"\n'  # shebang cannot hold a path with spaces
FAKE_ZIG = '''import json, os, sys
args = sys.argv[1:]
with open(os.environ['FAKE_ZIG_LOG'], 'a') as f:
    f.write(json.dumps(args) + '\\n')
if os.environ.get('FAKE_ZIG_PARTIAL'):
    with open(args[args.index('-o') + 1], 'wb') as f:
        f.write(b'MZ partial')
if os.environ.get('FAKE_ZIG_FAIL'):
    sys.exit(3)
with open(args[args.index('-o') + 1], 'wb') as f:
    f.write(b'MZ fresh')
'''

def load(name):
    spec = importlib.util.spec_from_file_location('iaf_' + name, NATIVE / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

class BuildTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='iaf-build-test-')
        self.addCleanup(tmp.cleanup)
        self.tmp = Path(tmp.name)
        self.zig = self.tmp / 'zig'
        (self.tmp / 'fake_zig.py').write_text(FAKE_ZIG)
        self.zig.write_text(FAKE_ZIG_SH % (sys.executable, self.tmp / 'fake_zig.py'))
        self.zig.chmod(self.zig.stat().st_mode | stat.S_IXUSR)
        self.log = self.tmp / 'zig.log'
        self.out = self.tmp / 'dist' / 'cc2x_island_airfields.dll'
        self.out.parent.mkdir()
        self.out.write_bytes(b'MZ stale')
        self.env = mock.patch.dict(os.environ, {'FAKE_ZIG_LOG': str(self.log),
                                                'ZIG_GLOBAL_CACHE_DIR': str(self.tmp / 'cache')})
        self.env.start()
        self.addCleanup(self.env.stop)
        self.build = load('build')
        self.build.OUT = self.out

    def run_build(self, sdk=None):
        argv = ['build.py', '--sdk', str(sdk or SDK), '--zig', str(self.zig)]
        with mock.patch.object(sys, 'argv', argv):
            self.build.main()

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_compiles_every_translation_unit_with_given_zig(self):
        self.run_build()
        args = self.calls()
        self.assertEqual(len(args), 1)
        names = [Path(a).name for a in args[0] if a.endswith('.c')]
        self.assertEqual(names, ['island_airfields.c', 'wasm_host.c',
                                'm3_bind.c', 'm3_code.c', 'm3_compile.c', 'm3_core.c',
                                'm3_deterministic.c', 'm3_env.c', 'm3_exec.c', 'm3_function.c',
                                'm3_info.c', 'm3_module.c', 'm3_parse.c', 'm3_validate.c'])
        self.assertIn('x86-windows-gnu', args[0])
        includes = [Path(args[0][i + 1]) for i, a in enumerate(args[0]) if a == '-I']
        self.assertIn(SDK / 'include', includes)
        self.assertIn(NATIVE / 'third_party' / 'wasm3' / 'source', includes)
        self.assertIn('-Dd_m3HasGasMetering=1', args[0])
        self.assertIn('-Dd_m3HasValidation=1', args[0])
        self.assertIn('-Werror=implicit-function-declaration', args[0])
        self.assertEqual(self.out.read_bytes(), b'MZ fresh')

    def test_sources_exist(self):
        for source in self.build.SOURCES:
            self.assertTrue(source.is_file(), source)

    def test_failed_compile_raises_and_leaves_no_dll(self):
        with mock.patch.dict(os.environ, {'FAKE_ZIG_FAIL': '1'}):
            with self.assertRaises(subprocess.CalledProcessError):
                self.run_build()
        self.assertFalse(self.out.exists())

    def test_failed_compile_discards_partial_dll(self):
        with mock.patch.dict(os.environ, {'FAKE_ZIG_FAIL': '1', 'FAKE_ZIG_PARTIAL': '1'}):
            with self.assertRaises(subprocess.CalledProcessError):
                self.run_build()
        self.assertFalse(self.out.exists())

    def test_bad_sdk_discards_stale_dll(self):
        with self.assertRaises(ValueError):
            self.run_build(sdk=self.tmp / 'missing-sdk')
        self.assertFalse(self.out.exists())
        self.assertFalse(self.log.exists())

    def test_failed_compile_exits_nonzero_as_script(self):
        code = ('import sys; sys.argv = ["build.py", "--sdk", %r, "--zig", %r]; '
                'import importlib.util; '
                'spec = importlib.util.spec_from_file_location("b", %r); '
                'm = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); '
                'm.OUT = __import__("pathlib").Path(%r); m.main()'
                % (str(SDK), str(self.zig), str(NATIVE / 'build.py'), str(self.out)))
        env = dict(os.environ, FAKE_ZIG_FAIL='1', PYTHONPATH=str(NATIVE))
        r = subprocess.run([sys.executable, '-c', code], env=env, capture_output=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn(b'non-zero exit status 3', r.stderr)   # the compiler failed, not the setup
        self.assertFalse(self.out.exists())

class PackageTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='iaf-package-test-')
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        (self.root / 'native' / 'dist').mkdir(parents=True)
        (self.root / 'content' / 'scripts').mkdir(parents=True)
        (self.root / 'content' / 'scripts' / 'a.lua').write_text('-- a')
        (self.root / 'mod.xml').write_text('<mod/>')
        (self.root / 'thumbnail.png').write_bytes(b'png')
        license_dir = self.root / 'native' / 'third_party' / 'wasm3'
        license_dir.mkdir(parents=True)
        (license_dir / 'LICENSE').write_text('MIT license fixture')
        self.dll = self.root / 'native' / 'dist' / 'cc2x_island_airfields.dll'
        self.wasm = self.dll.with_name('cc2x_island_airfields_core.wasm')
        self.dll.write_bytes(b'MZ stale')
        self.wasm.write_bytes(b'wasm stale')
        self.out = self.root / 'dist' / 'Island Airfields'
        self.out.mkdir(parents=True)
        (self.out / 'old-package').write_text('stale')
        self.package = load('package')
        self.package.ROOT = self.root
        self.calls = []

    def run_package(self, fail=None, skip=None, argv=()):
        def run(*args):
            script = Path(args[0]).name
            self.calls.append([str(a) for a in args])
            if fail == script:
                raise subprocess.CalledProcessError(3, script)
            if skip == script:
                return
            if script == 'build.py':
                self.dll.write_bytes(b'MZ fresh')
            elif script == 'build_wasm.py':
                self.wasm.write_bytes(b'wasm fresh')
        with mock.patch.object(self.package, 'run', run), \
                mock.patch.object(sys, 'argv', ['package.py', *argv]):
            self.package.main()

    def test_failed_build_removes_old_package_and_skips_install(self):
        for script in ('build_lua.py', 'build.py', 'build_wasm.py'):
            with self.subTest(script=script):
                self.calls.clear()
                with mock.patch.object(self.package.shutil, 'copytree') as install:
                    with self.assertRaises(subprocess.CalledProcessError):
                        self.run_package(fail=script, argv=['--install'])
                    install.assert_not_called()
                self.assertFalse(self.out.exists())
                self.assertEqual(Path(self.calls[-1][0]).name, script)

    def test_success_without_wasm_cannot_package_stale_asset(self):
        with self.assertRaises(FileNotFoundError):
            self.run_package(skip='build_wasm.py')
        self.assertFalse(self.wasm.exists())
        self.assertFalse(self.out.exists())

    def test_packages_exact_fresh_pair_and_license(self):
        self.run_package()
        packed = self.out / 'content' / 'cc2x'
        self.assertEqual((packed / self.dll.name).read_bytes(), self.dll.read_bytes())
        self.assertEqual((packed / self.wasm.name).read_bytes(), self.wasm.read_bytes())
        self.assertEqual((packed / 'wasm3-LICENSE.txt').read_text(), 'MIT license fixture')
        self.assertFalse((self.out / 'old-package').exists())

    def test_cli_options_forwarded_only_to_matching_builder(self):
        self.run_package(argv=['--sdk', str(SDK), '--zig', '/chosen/zig', '--game', '/chosen/game'])
        self.assertEqual(self.calls, [
            [str(self.root / 'lua' / 'build_lua.py'), '--game', '/chosen/game'],
            [str(self.root / 'native' / 'build.py'), '--sdk', str(SDK), '--zig', '/chosen/zig'],
            [str(self.root / 'native' / 'build_wasm.py'), '--zig', '/chosen/zig']])

    def test_copy_mismatch_discards_package(self):
        copy = self.package.shutil.copy2
        def corrupt(source, dest):
            result = copy(source, dest)
            if Path(dest).name == self.wasm.name:
                Path(dest).write_bytes(b'corrupt wasm')
            return result
        with mock.patch.object(self.package.shutil, 'copy2', corrupt):
            with self.assertRaisesRegex(ValueError, 'packaged artifact differs'):
                self.run_package()
        self.assertFalse(self.out.exists())

if __name__ == '__main__':
    if '--sdk' in sys.argv:
        i = sys.argv.index('--sdk')
        SDK = Path(sys.argv[i + 1]).resolve()
        del sys.argv[i:i + 2]
    from sdk_support import sdk_path
    SDK = sdk_path(SDK)
    unittest.main()
