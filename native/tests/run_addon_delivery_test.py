# SPDX-License-Identifier: GPL-3.0-or-later
"""Inspect separate-package delivery with the real SDK, never executing game Lua."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCREENS = ('screen_holomap.lua', 'screen_vehicle_control.lua', 'screen_propulsion.lua')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def data_sha(data):
    return hashlib.sha256(data).hexdigest()


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def inventory_hashes(resources):
    return {name: data_sha(data) for name, data in sorted(resources.items())}


def stock_expected(original, screen, generator, owned_patch):
    # Independently rebuild the owned historical patch outcome; do not replay
    # the descriptor offsets whose correctness this fixture must establish.
    if screen == 'screen_propulsion.lua':
        return b'-- Island Airfields: keep stock carrier-specific deck indications.\n'+generator.canonical(original)
    lines = generator.canonical(original).decode().split('\n')
    delta = (ROOT/'lua'/(screen+'.patch')).read_text().splitlines()
    modified = '\n'.join(owned_patch.apply_patch(lines, delta, screen))
    start = modified.index('-- [my_mod] Best-effort airbase anchor')
    anchor = ('function update(screen_w, screen_h, ticks)' if screen == 'screen_holomap.lua'
              else 'function input_event(event, action)')
    end = modified.index(anchor, start)
    return (modified[:start]+modified[end:]).encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--zig', type=Path, required=True)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--revolution', type=Path, action='append', required=True,
                        help='Repeat for every supported reviewed Revolution version')
    args = parser.parse_args()
    sdk = args.sdk.resolve(strict=True)
    evidence_path = sdk/'build/unified/test-results.json'
    build_path = sdk/'dist/dinput8.build.json'
    evidence = json.loads(evidence_path.read_text())
    build = json.loads(build_path.read_text())
    if (evidence['status'] != 'passed' or evidence['artifact_sha256'] != build['artifact_sha256']
            or sha(sdk/'dist/dinput8.dll') != build['artifact_sha256']):
        raise ValueError('Passing exact SDK evidence is required')
    if sha(args.zig) != build['compiler_sha256']:
        raise ValueError('Compiler mismatch')
    for name, digest in build['source_sha256'].items():
        if sha(sdk/name) != digest:
            raise ValueError('SDK input changed: '+name)
    objects = [sdk/'build/unified/objects/src_runtime_mod_loader.cpp.obj',
               *sorted((sdk/'build/unified/objects').glob('third_party_lua_*.obj'))]
    object_hashes = {path.relative_to(sdk).as_posix(): sha(path) for path in objects}
    sdk_evidence_hashes = {'build/unified/test-results.json': sha(evidence_path),
                          'dist/dinput8.build.json': sha(build_path)}

    generator = load('addon_profile_generator', ROOT/'compatibility/generate_screen_profiles.py')
    tools = load('addon_profile_tools', ROOT/'compatibility/revolution_1_6/profile_tools.py')
    owned_patch = load('addon_stock_patch', ROOT/'lua/build_lua.py')
    review_paths = [ROOT/'compatibility/revolution_1_6'/name for name in generator.REVIEW_FILES]
    reviews = [json.loads(path.read_text()) for path in review_paths]
    revolutions = generator.reviewed_revolutions(args.revolution, reviews, tools)
    descriptor_path = ROOT/'content/cc2x/screen_extensions.json'
    descriptor = json.loads(descriptor_path.read_text(encoding='utf-8'))
    extensions = {entry['screen']: entry for entry in descriptor['extensions']}
    if len(descriptor['extensions']) != 3 or set(extensions) != {'scripts/'+name for name in SCREENS}:
        raise ValueError('The exact three reviewed screen declarations are required')
    # Validate repeated source hashes without changing the descriptor or its order.
    for extension in extensions.values():
        if generator.unique_profiles(extension['profiles']) != extension['profiles']:
            raise ValueError('Duplicate source profiles in shipped descriptor')

    inputs = [Path(__file__).resolve(), ROOT/'mod.xml', ROOT/'native/tests/inspect_screen_edits.cpp', descriptor_path,
              ROOT/'compatibility/generate_screen_profiles.py',
              ROOT/'compatibility/revolution_1_6/profile_tools.py', ROOT/'lua/build_lua.py',
              *review_paths, *sorted((ROOT/'compatibility/revolution_1_6').glob('*.patch')),
              *sorted((ROOT/'lua').glob('screen_*.lua.patch')),
              *sorted((ROOT/'content/cc2x/ui').glob('*.lua'))]
    inputs = list(dict.fromkeys(inputs))
    hashes = {path.relative_to(ROOT).as_posix(): sha(path) for path in inputs}
    read = lambda root, name: (root/name).read_bytes()
    addon_resources = tools.inventory(ROOT, read)
    addon_hashes = inventory_hashes(addon_resources)
    if any('content/scripts/'+name in addon_resources for name in SCREENS):
        raise ValueError('Complete original screens must not be bundled')
    stock_resources = {'scripts/'+name: (args.game/'rom_0/scripts'/name).read_bytes() for name in SCREENS}
    version_inputs = {'stock': {
        'resources_sha256': inventory_hashes(stock_resources),
        'screen_canonical_sha256': {name: data_sha(generator.canonical(data))
                                     for name, data in stock_resources.items()},
    }}
    overlaps = {}
    for review, root, resources in revolutions:
        overlap = sorted(set(resources) & set(addon_resources))
        if overlap:
            raise ValueError('Separate addon resource overlap for '+review['name']+': '+repr(overlap))
        overlaps[review['name']] = overlap
        version_inputs[review['name']] = {
            'resource_count': len(resources),
            'resources_sha256': inventory_hashes(resources),
            'mod_xml_sha256': sha(root/'mod.xml'),
            'screen_canonical_sha256': {
                'scripts/'+name: data_sha(generator.canonical(resources['content/scripts/'+name]))
                for name in SCREENS},
        }
    older, newer = revolutions
    old_resources, new_resources = older[2], newer[2]
    added = sorted(set(new_resources)-set(old_resources))
    removed = sorted(set(old_resources)-set(new_resources))
    changed = sorted(name for name in set(old_resources) & set(new_resources)
                     if old_resources[name] != new_resources[name])
    if added or removed or changed != ['content/scripts/screen_vehicle_control.lua']:
        raise ValueError('The reviewed update must change only the vehicle-control screen')
    report = {
        'status': 'running', 'game_runtime_tested': False, 'third_party_lua_executed': False,
        'sdk_artifact_sha256': build['artifact_sha256'], 'sdk_source_commit': build['source_commit'],
        'sdk_object_sha256': object_hashes, 'sdk_evidence_sha256': sdk_evidence_hashes,
        'source_sha256': hashes, 'addon_resources_sha256': addon_hashes,
        'version_inputs': version_inputs, 'outputs': {}, 'resource_overlaps': overlaps,
        'reviewed_version_delta': {'from': older[0]['name'], 'to': newer[0]['name'],
                                   'added': added, 'removed': removed, 'changed': changed,
                                   'unchanged_resources': len(old_resources)-len(changed)},
    }
    inspector = ROOT/'native/tests/inspect_screen_edits.cpp'
    with tempfile.TemporaryDirectory(prefix='iaf-addon-delivery-') as folder:
        temp = Path(folder)
        executable = temp/'inspect.exe'
        subprocess.run([str(args.zig), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O1',
                        '-I', str(sdk/'src'), '-I', str(sdk/'third_party/lua'), str(inspector),
                        *map(str, objects), '-lbcrypt', '-ladvapi32', '-lole32', '-o', str(executable)],
                       check=True)
        report['inspector_sha256'] = sha(executable)
        cases = [('stock', args.game/'rom_0', None)]
        cases.extend((review['name'], root/'content', resources) for review, root, resources in revolutions)
        for index, (label, base, resources) in enumerate(cases):
            output = temp/('case-'+str(index))
            subprocess.run([str(executable), str(ROOT), str(base), str(output)], check=True, timeout=30)
            actual_names = {path.name for path in output.glob('*.lua')}
            if actual_names != set(SCREENS):
                raise ValueError('Incomplete SDK screen output: '+label)
            screen_report = {}
            for screen in SCREENS:
                if resources is None:
                    expected = stock_expected(stock_resources['scripts/'+screen], screen, generator, owned_patch)
                else:
                    name = 'content/scripts/'+screen
                    patch = (ROOT/'compatibility/revolution_1_6'/(screen+'.patch')).read_bytes()
                    expected = tools.apply_patch(name, resources[name], patch)
                helper = (ROOT/'content'/extensions['scripts/'+screen]['source']).read_bytes()
                actual = (output/screen).read_bytes()
                if actual != generator.canonical(expected)+b'\n'+helper:
                    raise ValueError('SDK output differs from independently applied owned patch: '+label+'/'+screen)
                screen_report[screen] = {'output_sha256': data_sha(actual),
                                          'transformed_sha256': data_sha(generator.canonical(expected)),
                                          'helper_sha256': data_sha(helper),
                                          'exact_patch_output_match': True}
            report['outputs'][label] = screen_report

    # Repeat complete reads after the compiler/SDK inspections. This proves
    # neither original version, metadata, stock screens, nor addon content changed.
    repeated = generator.reviewed_revolutions(args.revolution, reviews, tools)
    for review, root, resources in repeated:
        before = version_inputs[review['name']]
        if inventory_hashes(resources) != before['resources_sha256'] or sha(root/'mod.xml') != before['mod_xml_sha256']:
            raise ValueError('Revolution input changed during verification: '+review['name'])
    if inventory_hashes(tools.inventory(ROOT, read)) != addon_hashes:
        raise ValueError('Addon content changed during verification')
    for name, data in stock_resources.items():
        if (args.game/'rom_0'/name).read_bytes() != data:
            raise ValueError('Stock screen changed during verification: '+name)
    if hashes != {path.relative_to(ROOT).as_posix(): sha(path) for path in inputs}:
        raise ValueError('Authoring or reviewer inputs changed')
    if object_hashes != {path.relative_to(sdk).as_posix(): sha(path) for path in objects}:
        raise ValueError('SDK object changed during verification')
    if (sha(sdk/'dist/dinput8.dll') != build['artifact_sha256']
            or sdk_evidence_hashes != {name: sha(sdk/name) for name in sdk_evidence_hashes}
            or any(sha(sdk/name) != digest for name, digest in build['source_sha256'].items())):
        raise ValueError('SDK build or evidence changed during verification')
    report.update(status='passed', exact_patch_output_matches=True,
                  input_inventories_unchanged=True, original_screens_bundled=False)
    destination = ROOT/'build/revolution-audit/ADDON_DELIVERY.json'
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('PASS separate addon: stock and both reviewed Revolution versions; exact SDK/Git-patch output, zero overlaps and unchanged full inventories; no third-party Lua execution')


if __name__ == '__main__':
    main()
