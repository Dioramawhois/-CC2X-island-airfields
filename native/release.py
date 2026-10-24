# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze and verify an Island Airfields release candidate; never installs or runs the game.

python native/release.py --game-dir "path to installed game"
python native/release.py --sdk ../cc2x  # build outputs, not gameplay-verified
"""
import argparse
import configparser
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile

from tools.lua_check import check as check_lua
from sdk_support import sdk_path

ROOT = Path(__file__).resolve().parents[1]
GAME_HASH = '1c9a01c3007cb2bec2a04d716812cf9f24c87efa11056395c89ddd7d2c341026'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def pe_image(data):
    """Read PE32 sections/exports as data. No DLL loading or game execution."""
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('not a PE file')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if pe + 248 > len(data) or data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('invalid PE header')
    machine, sections = struct.unpack_from('<HH', data, pe + 4)
    optional, flags = struct.unpack_from('<HH', data, pe + 20)
    if machine != 0x14c or optional < 224 or not flags & 0x2000 or struct.unpack_from('<H', data, pe + 24)[0] != 0x10b:
        raise ValueError('release requires a Windows x86 DLL')
    table = pe + 24 + optional
    if sections > 96 or table + sections * 40 > len(data):
        raise ValueError('invalid section table')

    def offset(rva, size):
        for i in range(sections):
            virtual_size, va, raw_size, raw = struct.unpack_from('<IIII', data, table + i * 40 + 8)
            if va <= rva and rva - va + size <= raw_size:
                at = raw + rva - va
                if at + size <= len(data):
                    return at
        raise ValueError('PE address outside file-backed sections')

    return pe, offset


def plugin_version(data):
    pe, offset = pe_image(data)
    export_rva = struct.unpack_from('<I', data, pe + 24 + 96)[0]
    exports = offset(export_rva, 40)
    nfuncs, nnames, funcs, names, ordinals = struct.unpack_from('<IIIII', data, exports + 20)
    if nnames > 4096 or nfuncs > 4096:
        raise ValueError('invalid export counts')
    for i in range(nnames):
        name_rva = struct.unpack_from('<I', data, offset(names + i * 4, 4))[0]
        name = offset(name_rva, 1)
        end = data.find(b'\0', name, name + 256)
        if end >= 0 and data[name:end] == b'cc2x_island_airfields_debug':
            ordinal = struct.unpack_from('<H', data, offset(ordinals + i * 2, 2))[0]
            if ordinal >= nfuncs:
                raise ValueError('invalid export ordinal')
            rva = struct.unpack_from('<I', data, offset(funcs + ordinal * 4, 4))[0]
            magic, version = struct.unpack_from('<II', data, offset(rva, 8))
            if magic != 0x44464149:
                raise ValueError('invalid plugin version record')
            return version
    raise ValueError('plugin version export missing')


def write_archive(path, payload):
    names = set()
    for name in payload:
        parts = PurePosixPath(name)
        if parts.is_absolute() or '..' in parts.parts or ':' in name or '\\' in name or name.casefold() in names:
            raise ValueError('unsafe or duplicate archive path: ' + name)
        names.add(name.casefold())
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(payload.items()):
            # Stable ZIP metadata; checksums identify the bytes independent of build time.
            entry = zipfile.ZipInfo(name, (2026, 10, 24, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(payload):
            raise ValueError('archive verification failed')
        for name, expected in payload.items():
            if archive.read(name) != expected:
                raise ValueError('archive content mismatch: ' + name)
    path.with_suffix(path.suffix + '.sha256').write_text(digest(path.read_bytes()) + '  ' + path.name + '\n', encoding='ascii')


def prepare_release(game_dir=None, planes=None, sdk_dir=None):
    sdk = sdk_path(sdk_dir)
    source = ROOT / 'native/plugin/island_airfields.c'
    version = int(re.search(r'#define VERSION (\d+)', source.read_text(encoding='utf-8')).group(1))
    sdk_version = re.search(r'#define CC2X_VERSION "([^"]+)"', (sdk / 'src/cc2x.c').read_text(encoding='utf-8')).group(1)
    if game_dir:
        game_dir = Path(game_dir).resolve()
        if digest((game_dir / 'carrier_command.exe').read_bytes()) != GAME_HASH:
            raise ValueError('unsupported game EXE')
    core = (game_dir / 'dinput8.dll' if game_dir else sdk / 'dist/dinput8.dll').read_bytes()
    plugin = (game_dir / 'cc2x_island_airfields.dll' if game_dir else ROOT / 'native/dist/cc2x_island_airfields.dll').read_bytes()
    pe_image(core)
    if (sdk_version + '\0').encode('ascii') not in core:
        raise ValueError('SDK version differs from current source')
    if plugin_version(plugin) != version:
        raise ValueError('plugin binary/source versions differ')
    ini = (ROOT / 'native/plugin/cc2x_island_airfields.ini').read_text(encoding='utf-8')
    if planes is not None:
        ini = re.sub(r'(?m)^experimental_planes=\d+$', 'experimental_planes=' + str(int(planes)), ini)
    config = configparser.ConfigParser()
    config.read_string(ini)
    settings = {key: config.getint('island_airfields', key) for key in ('experimental_planes', 'plane_assist')}
    if any(value not in (0, 1) for value in settings.values()) or settings['plane_assist'] != 1:
        raise ValueError('release requires valid settings and plane_assist=1')
    payload = {
        'game folder/dinput8.dll': core,
        'game folder/cc2x_island_airfields.dll': plugin,
        'game folder/cc2x_island_airfields.ini': ini.encode('utf-8'),
        'README.txt': (ROOT / 'README_INSTALL.txt').read_bytes(),
        'RELEASE_NOTES.md': (ROOT / 'RELEASE_NOTES.md').read_bytes(),
    }
    lua_paths = []
    for path in sorted((ROOT / 'content').rglob('*')):
        if path.is_file() and path.name != '.DS_Store':
            if path.suffix != '.lua':
                raise ValueError('unexpected mod content: ' + str(path.relative_to(ROOT)))
            data = path.read_bytes()
            check_lua(data.decode('utf-8-sig'))
            lua_paths.append(path.relative_to(ROOT).as_posix())
            payload['mods/Island Airfields/' + path.relative_to(ROOT).as_posix()] = data
    if not lua_paths:
        raise ValueError('no Lua screens in package')
    for name in ('mod.xml', 'thumbnail.png'):
        payload['mods/Island Airfields/' + name] = (ROOT / name).read_bytes()
    if ET.fromstring((ROOT / 'mod.xml').read_bytes()).get('name') != 'Island Airfields':
        raise ValueError('unexpected mod identity')
    payload['licenses/Zig-LICENSE.txt'] = (ROOT / 'licenses/Zig-LICENSE.txt').read_bytes()
    payload['licenses/mingw-COPYING.txt'] = (ROOT / 'licenses/mingw-COPYING.txt').read_bytes()
    payload['licenses/PROJECT-LICENSE-STATUS.txt'] = (
        'Release candidate only. A license for the current CC2X and Island Airfields\n'
        'implementation must be confirmed before public source publication.\n'
        'The archived earlier SDK license is not assigned to this rewrite.\n'
        'Game scripts contain upstream game code; dependency notices retain their own terms.\n'
    ).encode('ascii')
    compilation = None
    sources = {}
    for folder in ('content', 'native/plugin', 'native/tools', 'native/tests', 'docs'):
        for path in sorted((ROOT / folder).rglob('*')):
            if path.is_file() and path.name != '.DS_Store' and '__pycache__' not in path.parts:
                sources[path.relative_to(ROOT).as_posix()] = path.read_bytes()
    for name in ('native/build.py', 'native/release.py', 'native/sdk_support.py',
                 'README.md', 'README_INSTALL.txt', 'RELEASE_NOTES.md', 'mod.xml', 'thumbnail.png',
                 '.gitignore', '.gitattributes', 'cc2x-sdk.json'):
        sources[name] = (ROOT / name).read_bytes()
    for name, data in payload.items():
        if name.startswith('licenses/'):
            sources[name] = data
    release = ROOT / 'release'
    release.mkdir(exist_ok=True)
    stem = 'IslandAirfields-v%d-rc1' % version
    source_zip = release / (stem + '-source.zip')
    write_archive(source_zip, sources)
    manifest = {
        'schema': 1, 'artifact': stem, 'channel': 'release-candidate',
        'plugin_version': version, 'cc2x_version': sdk_version,
        'sdk_dependency': {'name': 'CC2X', 'version': sdk_version, 'api': 1, 'sources_in_separate_repository': True},
        'game': 'Carrier Command 2 1.5.18 Windows x86', 'game_exe_sha256': GAME_HASH,
        'binary_origin': 'installed-game' if game_dir else 'build-output',
        'settings': settings, 'checks': {
            'pe32_dlls': True, 'plugin_export_version': True, 'mod_xml': True,
            'lua_structure_checked': lua_paths, 'full_lua_parser_run': False,
            'fresh_game_acceptance_run': False,
            'source_compile_passed': bool(compilation and compilation['compile_passed']),
            'source_rebuild_matches_binaries': all(
                compilation['outputs'][name]['rebuilt_sha256'] == digest(data)
                for name, data in (('dinput8.dll', core), ('cc2x_island_airfields.dll', plugin))) if compilation else None,
            'project_license_confirmed': False,
        },
        'source_archive': {'file': source_zip.name, 'sha256': digest(source_zip.read_bytes())},
        'files': {name: {'size': len(data), 'sha256': digest(data)} for name, data in sorted(payload.items())},
    }
    payload['release-manifest.json'] = json_bytes(manifest)
    payload['SHA256SUMS.txt'] = ''.join(digest(data) + '  ' + name + '\n' for name, data in sorted(payload.items())).encode('utf-8')
    destination = release / (stem + '.zip')
    write_archive(destination, payload)
    print('Verified candidate:', destination)
    print('Source snapshot:', source_zip)
    print('Native DLLs:', digest(core), digest(plugin))
    print('Public release pending: fresh game acceptance, source/binary correspondence, project license.')
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, help='read the installed, gameplay-used DLL pair without modifying it')
    parser.add_argument('--planes', choices=('on', 'off'), help='override the packaged setting only')
    parser.add_argument('--sdk', type=Path, help='separate CC2X repository (or set CC2X_SDK_DIR)')
    args = parser.parse_args()
    prepare_release(args.game_dir, None if args.planes is None else args.planes == 'on', args.sdk)


if __name__ == '__main__':
    main()
