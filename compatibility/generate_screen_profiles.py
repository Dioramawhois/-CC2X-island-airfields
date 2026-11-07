# SPDX-License-Identifier: GPL-3.0-or-later
"""Author owned in-memory edits from legitimate local sources; never ships them."""
import argparse
import difflib
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
REVIEW_FILES = ('profile.json', 'profile_1_6_20.json')

def canonical(data):
    return data.decode('utf-8-sig').replace('\r\n','\n').encode('utf-8')

def profile(original, modified):
    before, after = canonical(original).splitlines(keepends=True),canonical(modified).splitlines(keepends=True)
    offsets=[0]
    for line in before:offsets.append(offsets[-1]+len(line))
    edits=[]
    for kind,a,b,c,d in difflib.SequenceMatcher(a=before,b=after,autojunk=False).get_opcodes():
        if kind!='equal':
            old, new = b''.join(before[a:b]).decode('utf-8'), b''.join(after[c:d]).decode('utf-8')
            first, last = 0, 0
            while first < min(len(old), len(new)) and old[first] == new[first]:
                first += 1
            while last < min(len(old)-first, len(new)-first) and old[-1-last] == new[-1-last]:
                last += 1
            old_end, new_end = len(old)-last, len(new)-last
            edits.append({'offset':offsets[a]+len(old[:first].encode('utf-8')),
                          'remove':len(old[first:old_end].encode('utf-8')),
                          'insert':new[first:new_end]})
    return {'sha256':hashlib.sha256(canonical(original)).hexdigest(),'edits':edits}

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def unique_profiles(items):
    """Keep every supported source; equal hashes must have equal byte edits."""
    result = []
    by_hash = {}
    for item in items:
        previous = by_hash.get(item['sha256'])
        if previous is not None:
            if previous['edits'] != item['edits']:
                raise ValueError('Conflicting reviewed edits for source: ' + item['sha256'])
            continue
        by_hash[item['sha256']] = item
        result.append(item)
    return result

def reviewed_revolutions(roots, reviews, tools):
    """Match complete legitimate local inputs to every supported exact review."""
    found = {}
    names = [review['name'] for review in reviews]
    if len(set(names)) != len(names):
        raise ValueError('Duplicate Revolution review name')
    for root in roots:
        root = Path(root)
        metadata = root/'mod.xml'
        if (root.is_symlink() or getattr(root, 'is_junction', lambda: False)()
                or metadata.is_symlink() or not metadata.is_file()):
            raise ValueError('Ordinary local Revolution inputs are required')
        resources = tools.inventory(root, lambda base, name: (base/name).read_bytes())
        hashes = {name: hashlib.sha256(data).hexdigest() for name, data in resources.items()}
        metadata_hash = hashlib.sha256(metadata.read_bytes()).hexdigest()
        matches = [review for review in reviews
                   if hashes == review['resources_sha256']
                   and metadata_hash == review['mod_xml_sha256']]
        if len(matches) != 1:
            raise ValueError('Review the changed Revolution content and metadata first: ' + str(root))
        review = matches[0]
        if review['name'] in found:
            raise ValueError('Duplicate Revolution input: ' + review['name'])
        found[review['name']] = (review, root, resources)
    missing = [name for name in names if name not in found]
    if missing:
        raise ValueError('Provide every supported Revolution input; missing: ' + ', '.join(missing))
    return [found[name] for name in names]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--revolution',type=Path,action='append',required=True,
                        help='Repeat for every supported reviewed Revolution version')
    args=parser.parse_args()
    tools=load('profile_tools',ROOT/'compatibility/revolution_1_6/profile_tools.py')
    owned_patch=load('owned_patch',ROOT/'lua/build_lua.py')
    reviews = [json.loads((ROOT/'compatibility/revolution_1_6'/name).read_text())
               for name in REVIEW_FILES]
    revolutions = reviewed_revolutions(args.revolution, reviews, tools)
    entries=[]
    for screen in ('screen_holomap.lua','screen_vehicle_control.lua','screen_propulsion.lua'):
        name='content/scripts/'+screen
        delta=(ROOT/'compatibility/revolution_1_6'/(screen+'.patch')).read_bytes()
        profiles=[]
        for known, _, resources in revolutions:
            rev = resources[name]
            edited = tools.apply_patch(name, rev, delta)
            profiles.append(profile(rev, edited))
        for base in (args.game/'mod_dev_kit/source/scripts'/screen,args.game/'rom_0/scripts'/screen):
            original=base.read_bytes()
            if screen=='screen_propulsion.lua':
                modified=b'-- Island Airfields: keep stock carrier-specific deck indications.\n'+canonical(original)
            else:
                lines=canonical(original).decode().split('\n')
                delta=(ROOT/'lua'/(screen+'.patch')).read_text().splitlines()
                modified='\n'.join(owned_patch.apply_patch(lines,delta,screen))
                modified=modified.encode()
            item=profile(original,modified)
            profiles.append(item)
        helper='carrier_traffic.lua' if screen=='screen_propulsion.lua' else 'ui_common.lua'
        entries.append({'screen':'scripts/'+screen,'source':'cc2x/ui/'+helper,
                        'profiles':unique_profiles(profiles)})
    destination=ROOT/'content/cc2x/screen_extensions.json'
    destination.write_text(json.dumps({'schema':2,'extensions':entries},indent=2)+'\n',encoding='utf-8',newline='\n')
    print('Authored source-free addon profiles:',[(e['screen'],len(e['profiles'])) for e in entries])

if __name__=='__main__':main()
