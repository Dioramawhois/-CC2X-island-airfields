# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a local combined test package; never installs, launches or publishes it."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def sha(data):
    return hashlib.sha256(data).hexdigest()

def verify_profile(resources, profile):
    if {name: sha(data) for name, data in resources.items()} != profile['resources_sha256']:
        raise ValueError('Revolution content differs from reviewed 1.6-19; review the new version first')

def apply_patch(name, original, patch):
    # Pin both the file boundary and exact bytes before Git sees a patch.
    prefix = ('--- a/' + name + '\n+++ b/' + name + '\n').encode()
    if not patch.startswith(prefix) or patch.count(b'\n--- ') or patch.count(b'\n+++ ') != 1:
        raise ValueError('Compatibility patch must target exactly its reviewed screen')
    with tempfile.TemporaryDirectory(prefix='iaf-revolution-patch-') as temporary:
        root = Path(temporary)
        path = root/name
        path.parent.mkdir(parents=True)
        path.write_bytes(original.decode('utf-8-sig').replace('\r\n', '\n').encode())
        delta = root/'reviewed.patch'
        delta.write_bytes(patch)
        result = subprocess.run(['git', '-c', 'core.autocrlf=false', 'apply', '--whitespace=nowarn', str(delta)],
                                cwd=root, capture_output=True, timeout=15)
        if result.returncode:
            raise ValueError('Reviewed compatibility patch no longer applies: ' + name)
        return path.read_bytes()

def inventory(root, read):
    result, pending = {}, [root/'content']
    while pending:
        directory = pending.pop()
        if directory.is_symlink() or getattr(directory, 'is_junction', lambda: False)():
            raise ValueError('Linked content is forbidden')
        if len(directory.relative_to(root).parts) > 32:
            raise ValueError('Content depth limit exceeded')
        with os.scandir(directory) as entries:
            for entry in entries:
                path = Path(entry.path)
                if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
                    raise ValueError('Linked resource is forbidden')
                if entry.is_dir(follow_symlinks=False):
                    pending.append(path)
                elif entry.is_file(follow_symlinks=False):
                    name = path.relative_to(root).as_posix()
                    result[name] = read(root, name)
                else:
                    raise ValueError('Only ordinary resources are allowed')
                if len(result) + len(pending) > 1000:
                    raise ValueError('Compatibility input resource limit exceeded')
        if sum(map(len, result.values())) > 64*1024*1024:
            raise ValueError('Compatibility input byte limit exceeded')
    return result

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--revolution', type=Path, required=True)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--luac', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    sdk, rev, game = (path.resolve(strict=True) for path in (args.sdk, args.revolution, args.game))
    packager = load('cc2x_compat_packager', sdk/'tools/mod_package.py')
    read = packager.regular
    profile = json.loads(read(HERE, 'profile.json'))
    resources = inventory(rev, read)
    verify_profile(resources, profile)
    manifest = read(rev, 'mod.xml')
    if sha(manifest) != profile['mod_xml_sha256']:
        raise ValueError('Revolution manifest differs from the reviewed version')
    generation = json.loads(read(ROOT, 'integrations/owned-island-generation.json'))
    if sha(read(game, 'carrier_command.exe')) != generation['exe_sha256']:
        raise ValueError('Only the reviewed CC2 client generation profile is supported')
    original = dict(resources)
    helper = read(HERE, 'ui_common.lua')
    patch_hashes = {}
    for screen in ('screen_holomap.lua', 'screen_vehicle_control.lua'):
        name = 'content/scripts/' + screen
        delta = read(HERE, screen+'.patch')
        resources[name] = apply_patch(name, original[name], delta) + b'\n' + helper
        patch_hashes[screen] = sha(delta)
    for name, data in inventory(ROOT, read).items():
        if name.startswith('content/scripts/'):
            raise ValueError('Unexpected generated vanilla screen cache in source checkout')
        if name in resources:
            raise ValueError('Independent module collides with Revolution content')
        resources[name] = data
    resources['thumbnail.png'] = read(ROOT, 'thumbnail.png')
    resources['content/iaf_compat/REVOLUTION-ORIGINAL-MOD.xml'] = manifest
    resources['content/iaf_compat/README-COMPATIBILITY.txt'] = read(HERE, 'README-LOCAL.txt')
    metadata = json.loads(read(ROOT, 'package.json'))
    metadata.update(id='iaf_rev16_local_test',
        name='Revolution 1.6-19 + Island Airfields (compatibility test)',
        author='Revolution authors + dioramawork', version='2.0.0-rev16-test.1',
        description='Local compatibility candidate. Includes Revolution 1.6-19 and Island Airfields Lua gameplay. Requires CC2X SDK 0.8. Gameplay acceptance pending. Enable this combined copy only.')
    provenance = {'validation': 'local-combined-candidate-not-gameplay-acceptance',
        'source_commit': subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
        'source_dirty': bool(subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain'],text=True).strip()),
        'mod_order': ['3460002110','3826940003'], 'revolution_manifest_sha256': sha(manifest),
        'revolution_source_sha256': profile['resources_sha256'], 'patch_sha256': patch_hashes,
        'ui_helper_sha256': sha(helper), 'recipe_sha256': sha(Path(__file__).read_bytes()),
        'game_exe_sha256': generation['exe_sha256'],
        'airfields_source_sha256': {name:sha(data) for name,data in resources.items()
                                  if name.startswith('content/bridge')},
        'unmodified_revolution_resources': len(original)-2,
        'game_runtime_tested': False, 'remote_multiplayer_tested': False,
        'redistribution': 'Local test output includes third-party Revolution resources; not a public release.'}
    # Recheck subscribed inputs after transformation, before any output is published.
    verify_profile(inventory(rev, read), profile)
    if read(rev, 'mod.xml') != manifest:
        raise ValueError('Revolution manifest changed during build')
    print(packager.package(ROOT, metadata, None, None, resources, args.output,
                           provenance=provenance, luac=args.luac.resolve(strict=True)))

if __name__ == '__main__':
    main()
