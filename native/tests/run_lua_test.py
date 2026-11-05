# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute our Lua policy through the actual SDK Host, with a synthetic engine.

Requires a freshly verified external SDK build. Never loads a game or mod DLL.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--zig', type=Path, required=True)
    args = parser.parse_args()
    sdk, compiler = args.sdk.resolve(strict=True), args.zig.resolve(strict=True)
    evidence = json.loads((sdk / 'build/unified/test-results.json').read_text())
    build = json.loads((sdk / 'dist/dinput8.build.json').read_text())
    if evidence['status'] != 'passed' or evidence['artifact_sha256'] != build['artifact_sha256']:
        raise ValueError('A passing matching SDK build is required')
    if build['compiler_sha256'] != sha(compiler):
        raise ValueError('Compiler differs from the verified SDK build')
    for name, digest in build['source_sha256'].items():
        if sha(sdk / name) != digest:
            raise ValueError('SDK input changed: ' + name)
    spec = importlib.util.spec_from_file_location('sdk_build', sdk / 'tools/build_unified.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    sources = [*module.CPP_SOURCES, 'entity_registry.c', 'order_context.c', 'platform/seh_guard.c']
    objects = [sdk / 'build/unified/objects' / ('src_' + name.replace('/', '_') + '.obj') for name in sources]
    objects += sorted((sdk / 'build/unified/objects').glob('third_party_lua_*.obj'))
    if not objects or any(not p.is_file() for p in objects):
        raise ValueError('Missing SDK fixture objects')
    inputs = [*sorted((ROOT / 'content/bridge').rglob('*.lua')), ROOT / 'content/bridge.json',
              *sorted((ROOT / 'lua').glob('*.lua.patch')), ROOT / 'native/tests/owned_map_knowledge.lua',
              ROOT / 'native/tests/lua_airfields_test.cpp', ROOT / 'content/cc2x/ui/ui_common.lua', Path(__file__).resolve()]
    hashes = {p.relative_to(ROOT).as_posix(): sha(p) for p in inputs}
    with tempfile.TemporaryDirectory(prefix='cc2x-airfields-lua-') as temp:
        output = Path(temp) / 'airfields_lua_test.exe'
        command = [compiler, 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O1', '-UNDEBUG',
                   '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-I', sdk / 'include', '-I', sdk / 'src',
                   '-I', sdk / 'third_party/lua', ROOT / 'native/tests/lua_airfields_test.cpp',
                   *objects, *module.LIBS, '-o', output]
        compiled = subprocess.run(list(map(str, command)))
        if compiled.returncode:
            raise SystemExit(compiled.returncode)
        result = subprocess.run([str(output), str(ROOT)], capture_output=True, text=True, timeout=120)
        print(result.stdout, end='')
        print(result.stderr, end='')
        if result.returncode:
            raise SystemExit(result.returncode)
        if hashes != {p.relative_to(ROOT).as_posix(): sha(p) for p in inputs}:
            raise ValueError('Consumer inputs changed during verification')
        report = {'status': 'passed', 'source_sha256': hashes, 'compiler_sha256': sha(compiler),
                  'sdk_artifact_sha256': build['artifact_sha256'], 'test_executable_sha256': sha(output),
                  'validation': 'actual-sdk-host-real-lua-synthetic-flat-ground-engine',
                  'game_runtime_tested': False, 'remote_multiplayer_tested': False,
                  'dedicated_server_tested': False}
        destination = ROOT / 'native/dist/lua-test-results.json'
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
