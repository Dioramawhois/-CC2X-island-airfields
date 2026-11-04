# SPDX-License-Identifier: GPL-3.0-or-later
"""Author owned in-memory edits from legitimate local sources; never ships them."""
import argparse
import difflib
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def canonical(data):
    return data.decode('utf-8-sig').replace('\r\n','\n').encode('utf-8')

def profile(original, modified):
    before, after = canonical(original).splitlines(keepends=True),canonical(modified).splitlines(keepends=True)
    offsets=[0]
    for line in before:offsets.append(offsets[-1]+len(line))
    edits=[]
    for kind,a,b,c,d in difflib.SequenceMatcher(a=before,b=after,autojunk=False).get_opcodes():
        if kind!='equal':
            edits.append({'offset':offsets[a],'remove':offsets[b]-offsets[a],
                          'insert':b''.join(after[c:d]).decode('utf-8')})
    return {'sha256':hashlib.sha256(canonical(original)).hexdigest(),'edits':edits}

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--revolution',type=Path,required=True)
    args=parser.parse_args()
    tools=load('profile_tools',ROOT/'compatibility/revolution_1_6/profile_tools.py')
    owned_patch=load('owned_patch',ROOT/'lua/build_lua.py')
    entries=[]
    for screen in ('screen_holomap.lua','screen_vehicle_control.lua','screen_propulsion.lua'):
        name='content/scripts/'+screen
        rev=(args.revolution/name).read_bytes()
        known=json.loads((ROOT/'compatibility/revolution_1_6/profile.json').read_text())
        if hashlib.sha256(rev).hexdigest()!=known['resources_sha256'][name]:
            raise ValueError('Review the changed Revolution screen first: '+screen)
        delta=(ROOT/'compatibility/revolution_1_6'/(screen+'.patch')).read_bytes()
        edited=tools.apply_patch(name,rev,delta)
        profiles=[profile(rev,edited)]
        for base in (args.game/'mod_dev_kit/source/scripts'/screen,args.game/'rom_0/scripts'/screen):
            original=base.read_bytes()
            if screen=='screen_propulsion.lua':
                modified=b'-- Island Airfields: keep stock carrier-specific deck indications.\n'+canonical(original)
            else:
                lines=canonical(original).decode().split('\n')
                delta=(ROOT/'lua'/(screen+'.patch')).read_text().splitlines()
                modified='\n'.join(owned_patch.apply_patch(lines,delta,screen))
                start=modified.index('-- [my_mod] Best-effort airbase anchor')
                end=modified.index('function update(screen_w, screen_h, ticks)',start) if screen=='screen_holomap.lua' else modified.index('function input_event(event, action)',start)
                modified=(modified[:start]+modified[end:]).encode()
            item=profile(original,modified)
            if not any(p['sha256']==item['sha256'] for p in profiles):profiles.append(item)
        helper='carrier_traffic.lua' if screen=='screen_propulsion.lua' else 'ui_common.lua'
        entries.append({'screen':'scripts/'+screen,'source':'cc2x/ui/'+helper,'profiles':profiles})
    destination=ROOT/'content/cc2x/screen_extensions.json'
    destination.write_text(json.dumps({'schema':2,'extensions':entries},indent=2)+'\n',encoding='utf-8',newline='\n')
    print('Authored source-free addon profiles:',[(e['screen'],len(e['profiles'])) for e in entries])

if __name__=='__main__':main()
