# SPDX-License-Identifier: GPL-3.0-or-later
"""Inspect separate-package delivery with the real SDK, never executing game Lua."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True);p.add_argument('--zig',type=Path,required=True)
    p.add_argument('--game',type=Path,required=True);p.add_argument('--revolution',type=Path,required=True)
    args=p.parse_args();sdk=args.sdk.resolve(strict=True)
    evidence=json.loads((sdk/'build/unified/test-results.json').read_text())
    build=json.loads((sdk/'dist/dinput8.build.json').read_text())
    if evidence['status']!='passed' or evidence['artifact_sha256']!=build['artifact_sha256']:
        raise ValueError('Passing exact SDK evidence is required')
    if sha(args.zig)!=build['compiler_sha256']:raise ValueError('Compiler mismatch')
    for name,digest in build['source_sha256'].items():
        if sha(sdk/name)!=digest:raise ValueError('SDK input changed: '+name)
    objects=[sdk/'build/unified/objects/src_runtime_mod_loader.cpp.obj',
             *sorted((sdk/'build/unified/objects').glob('third_party_lua_*.obj'))]
    inputs=[ROOT/'native/tests/inspect_screen_edits.cpp',ROOT/'content/cc2x/screen_extensions.json',
            *sorted((ROOT/'content/cc2x/ui').glob('*.lua'))]
    hashes={str(path.relative_to(ROOT)):sha(path) for path in inputs}
    report={'status':'running','game_runtime_tested':False,'third_party_lua_executed':False,
            'sdk_artifact_sha256':build['artifact_sha256'],'source_sha256':hashes,'outputs':{}}
    with tempfile.TemporaryDirectory(prefix='iaf-addon-delivery-') as folder:
        temp=Path(folder);exe=temp/'inspect.exe'
        subprocess.run([str(args.zig),'c++','-target','x86-windows-gnu','-std=c++17','-O1',
            '-I',str(sdk/'src'),'-I',str(sdk/'third_party/lua'),str(inputs[0]),*map(str,objects),
            '-lbcrypt','-ladvapi32','-lole32','-o',str(exe)],check=True)
        report['inspector_sha256']=sha(exe)
        for label,base in [('revolution',args.revolution/'content'),('stock',args.game/'rom_0')]:
            output=temp/label
            subprocess.run([str(exe),str(ROOT),str(base),str(output)],check=True,timeout=30)
            report['outputs'][label]={path.name:sha(path) for path in output.glob('*.lua')}
    rev_names={path.relative_to(args.revolution/'content').as_posix()
               for path in (args.revolution/'content').rglob('*') if path.is_file()}
    addon_names={path.relative_to(ROOT/'content').as_posix()
                 for path in (ROOT/'content').rglob('*') if path.is_file()}
    overlaps=sorted(rev_names&addon_names)
    if overlaps:raise ValueError('Separate addon resource overlap: '+repr(overlaps))
    if hashes!={str(path.relative_to(ROOT)):sha(path) for path in inputs}:raise ValueError('Inputs changed')
    report.update(status='passed',resource_overlaps=overlaps,original_screens_bundled=False)
    destination=ROOT/'build/revolution-audit/ADDON_DELIVERY.json'
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS separate addon: zero resource overlaps; actual SDK transforms and compiles all stock/Revolution screens without executing them')
