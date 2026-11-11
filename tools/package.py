#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
"""Explicit payload allowlist. Deterministic ZIP metadata, no local game files."""
import hashlib
import json
from pathlib import Path
import zipfile
from install import PAYLOAD

ROOT = Path(__file__).resolve().parents[1]


def payload(root):
    files = {}
    for name in sorted(PAYLOAD):
        source = root / ('config' if name.endswith('.ini') else 'build') / Path(name).name
        files[name] = source.read_bytes()
    files['install.py'] = (root / 'tools/install.py').read_bytes()
    files['compatibility.json'] = (root / 'compatibility.json').read_bytes()
    for name in ('README.md', 'LICENSE', 'LICENSE.md', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md'):
        files[name] = (root / name).read_bytes()
    for name in ('CONTROLS.md', 'INSTALL.md', 'FEATURES.md', 'RELEASE_CHECKLIST.md'):
        files['docs/' + name] = (root / 'docs' / name).read_bytes()
    for name in ('bridge.py', 'run.py', 'ds_core.py', 'ds_motion.py', 'ds_audio.py', 'requirements.txt'):
        files['companion/' + name] = (root / 'companion' / name).read_bytes()
    # Corresponding source accompanies every binary package, including build/install tools.
    for directory in ('src', 'compat', 'companion', 'config', 'tools', 'tests', 'docs', '.github'):
        for source in sorted((root / directory).rglob('*')):
            if source.suffix not in ('.c', '.h', '.py', '.ini', '.txt', '.md', '.json', '.yml'):
                continue
            if source.is_symlink():
                raise ValueError('Source must not be a symlink: ' + str(source))
            files['source/' + source.relative_to(root).as_posix()] = source.read_bytes()
    for name in ('Makefile', 'VERSION', '.gitignore', 'README.md', 'LICENSE', 'LICENSE.md',
                 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md', 'CONTRIBUTING.md', 'compatibility.json'):
        files['source/' + name] = (root / name).read_bytes()
    for source in sorted((root / 'licenses').iterdir()):
        files['licenses/' + source.name] = source.read_bytes()
        files['source/licenses/' + source.name] = source.read_bytes()
    version = (root / 'VERSION').read_text().strip()
    if not version or any(c not in '0123456789abcdefghijklmnopqrstuvwxyz.-' for c in version):
        raise ValueError('Invalid release version')
    hashes = {name: hashlib.sha256(files[name]).hexdigest() for name in sorted(PAYLOAD)}
    files['manifest.json'] = (json.dumps({'schema': 1, 'version': version, 'files': hashes}, indent=2) + '\n').encode()
    files['SHA256SUMS'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
                                  for name, data in sorted(files.items())).encode()
    return version, files


def package(root=ROOT):
    version, files = payload(root)
    directory = root / 'dist'
    directory.mkdir(exist_ok=True)
    output = directory / ('nfsu2-driving-mods-' + version + '.zip')
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    (directory / (output.name + '.sha256')).write_text(hashlib.sha256(output.read_bytes()).hexdigest() + '  ' + output.name + '\n')
    print(output)
    return output


if __name__ == '__main__': package()
