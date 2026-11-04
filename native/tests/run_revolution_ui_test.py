# SPDX-License-Identifier: GPL-3.0-or-later
"""Run owned UI fixtures with the external SDK's already-built real Lua objects."""
import argparse
import hashlib
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
    sdk, zig = args.sdk.resolve(strict=True), args.zig.resolve(strict=True)
    build = json.loads((sdk/'dist/dinput8.build.json').read_text())
    evidence = json.loads((sdk/'build/unified/test-results.json').read_text())
    if evidence['status'] != 'passed' or evidence['artifact_sha256'] != build['artifact_sha256']:
        raise ValueError('Passing evidence for the external SDK build is required')
    if sha(zig) != build['compiler_sha256']:
        raise ValueError('Use the verified SDK compiler')
    inputs = [ROOT/'compatibility/revolution_1_6/ui_common.lua',
              ROOT/'native/tests/revolution_ui.lua', ROOT/'native/tests/revolution_ui_runner.c',
              Path(__file__).resolve()]
    hashes = {p.relative_to(ROOT).as_posix():sha(p) for p in inputs}
    objects = sorted((sdk/'build/unified/objects').glob('third_party_lua_*.obj'))
    if not objects:
        raise ValueError('Build the SDK before running its Lua consumer fixture')
    for name,digest in build['source_sha256'].items():
        if sha(sdk/name) != digest:
            raise ValueError('SDK source differs from its build: '+name)
    with tempfile.TemporaryDirectory(prefix='iaf-revolution-ui-') as folder:
        output = Path(folder)/'ui-test.exe'
        subprocess.run([str(zig),'cc','-target','x86-windows-gnu','-O1','-UNDEBUG',
                        '-I',str(sdk/'third_party/lua'),str(inputs[2]),
                        *map(str,objects),'-o',str(output)],check=True)
        subprocess.run([str(output),str(inputs[0]),str(inputs[1])],check=True,timeout=15)
        executable = sha(output)
    if hashes != {p.relative_to(ROOT).as_posix():sha(p) for p in inputs}:
        raise ValueError('UI fixture inputs changed during validation')
    report = {'status':'passed','validation':'owned-ui-real-lua-synthetic-engine',
              'game_runtime_tested':False,'remote_multiplayer_tested':False,
              'source_sha256':hashes,'test_executable_sha256':executable,
              'sdk_artifact_sha256':build['artifact_sha256'],
              'lua_objects_sha256':{p.name:sha(p) for p in objects}}
    destination = ROOT/'build/revolution-audit/UI_TEST.json'
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
