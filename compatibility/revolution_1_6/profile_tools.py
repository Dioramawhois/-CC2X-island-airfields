# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline reviewed-profile authoring; never builds a combined player package."""
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
