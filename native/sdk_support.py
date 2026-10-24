# SPDX-License-Identifier: GPL-3.0-or-later
"""Locate the separate CC2X SDK; no SDK sources are bundled with this mod."""
import importlib.util
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sdk_path(value=None):
    folder = Path(value or os.environ.get('CC2X_SDK_DIR') or ROOT.parent / 'cc2x').resolve()
    if not (folder / 'include/cc2x.h').is_file() or not (folder / 'build.py').is_file():
        raise ValueError('CC2X SDK not found at %s; pass --sdk PATH or set CC2X_SDK_DIR' % folder)
    return folder


def load_sdk(folder):
    spec = importlib.util.spec_from_file_location('cc2x_build', sdk_path(folder) / 'build.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
