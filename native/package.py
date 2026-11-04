# SPDX-License-Identifier: GPL-3.0-or-later
"""Developer packaging through the external SDK. Never installs or runs a mod."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
from xml.etree import ElementTree as ET
from sdk_support import sdk_path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--zig', type=Path)
    parser.add_argument('--game', type=Path)
    parser.add_argument('--luac', type=Path, required=True, help='real Lua 5.3 syntax checker')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    destination = args.output or ROOT / 'dist/Island Airfields'
    if destination.exists() or destination.is_symlink():
        raise ValueError('Existing output is preserved')
    sdk = sdk_path(args.sdk)
    subprocess.run([sys.executable, str(ROOT / 'lua/build_lua.py'),
                    *(['--game', str(args.game)] if args.game else [])], check=True)
    subprocess.run([sys.executable, str(ROOT / 'native/build.py'), '--sdk', str(sdk),
                    *(['--zig', str(args.zig)] if args.zig else [])], check=True)
    spec = importlib.util.spec_from_file_location('cc2x_mod_package', sdk / 'tools/mod_package.py')
    packager = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packager)
    source = packager.regular(ROOT, 'mod.xml')
    packager.validate_resource('mod.xml', source, None)
    xml = ET.fromstring(source)
    metadata = {**xml.attrib, 'id': '3826940003', 'version': '1.1.0-dev', 'api': 8,
                'plugin': 'cc2x_island_airfields.dll'}
    resources = {'thumbnail.png': packager.regular(ROOT, 'thumbnail.png')}
    for path in sorted((ROOT / 'content/scripts').glob('*.lua')):
        name = path.relative_to(ROOT).as_posix()
        resources[name] = packager.regular(ROOT, name)
    plugin = ROOT / 'native/dist/cc2x_island_airfields.dll'
    report = json.loads(plugin.with_suffix('.build.json').read_text())
    provenance = {'workshop_id': '3826940003',
                  'source_mod_xml_sha256': hashlib.sha256(source).hexdigest(),
                  'project_commit': report['source_commit'], 'project_dirty': report['source_dirty'],
                  'patch_sha256': {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in sorted((ROOT / 'lua').glob('*.patch'))}}
    destination.parent.mkdir(parents=True, exist_ok=True)
    print(packager.package(ROOT, metadata, plugin, report, resources, destination,
                           provenance=provenance, luac=args.luac))


if __name__ == '__main__':
    main()
