#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
"""Dry-run by default. Install only manifest files; archive everything replaced."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import uuid

STATE = '.nfsu2-driving-mods'
PAYLOAD = frozenset({
    'scripts/NFSU2.AnalogClutch.asi', 'scripts/NFSU2.RightStickLook.asi',
    'scripts/NFSU2.MirrorPanels.asi', 'scripts/NFSU2.Realism.ini',
    'scripts/NFSU2.RightStickLook.ini', 'scripts/NFSU2.GearGrind.wav',
})


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_path(root, name):
    if root.is_symlink():
        raise ValueError("Refusing symlink root: " + str(root))
    parts = PurePosixPath(name).parts
    if not parts or name not in PAYLOAD or '\\' in name or '..' in parts:
        raise ValueError('Unexpected payload path: ' + name)
    target = root
    for part in parts:
        target /= part
        if target.is_symlink():
            raise ValueError('Refusing symlink: ' + str(target))
    if target.exists() and not target.is_file():
        raise ValueError('Expected regular file: ' + str(target))
    return target


def game_running():
    if sys.platform == 'win32':
        result = subprocess.run(['tasklist', '/FO', 'CSV', '/NH'], check=True, capture_output=True, text=True)
        return any(row and row[0].casefold() == 'speed2.exe' for row in csv.reader(result.stdout.splitlines()))
    for entry in Path('/proc').glob('[0-9]*/cmdline'):
        try:
            arguments = entry.read_bytes().split(b'\0')
        except (OSError, ProcessLookupError):
            continue
        if any(arg.replace(b'\\', b'/').rsplit(b'/', 1)[-1].lower() == b'speed2.exe' for arg in arguments):
            return True
    return False


def state_dir(game):
    path = game / STATE
    if path.is_symlink() or (path.exists() and not path.is_dir()):
        raise ValueError('Unsafe installer state directory')
    return path


def write_json(path, value):
    temporary = path.with_name(path.name + '.tmp-' + uuid.uuid4().hex)
    with temporary.open('x') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
    os.replace(temporary, path)


def copy_atomic(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + '.tmp-' + uuid.uuid4().hex)
    with source.open('rb') as src, temporary.open('xb') as dst:
        shutil.copyfileobj(src, dst)
    os.replace(temporary, target)


def plan(package, game):
    manifest = json.loads((package / 'manifest.json').read_text())
    if manifest.get('schema') != 1 or set(manifest.get('files', {})) != PAYLOAD:
        raise ValueError('Unsupported or incomplete package manifest')
    compatibility = json.loads((package / 'compatibility.json').read_text())
    for name, expected in compatibility['required_files'].items():
        if name not in ('SPEED2.EXE', 'dinput8.dll'):
            raise ValueError('Unexpected dependency name')
        actual = game / name
        if not actual.is_file() or digest(actual) != expected:
            raise ValueError('Unsupported or missing game dependency: ' + name)
    changes = []
    for name, expected in sorted(manifest['files'].items()):
        source, target = safe_path(package, name), safe_path(game, name)
        if digest(source) != expected:
            raise ValueError('Package checksum mismatch: ' + name)
        # Keep existing controls and user preferences. No saves or ffb.ini are written.
        if name.endswith('.ini') and target.exists():
            continue
        changes.append({'path': name, 'installed_sha256': expected,
                        'original_sha256': digest(target) if target.exists() else None})
    return manifest, changes


def install(package, game, apply=False, accept_prerelease=False):
    state = state_dir(game)
    if (state / 'state.json').exists():
        raise ValueError('An installation exists; uninstall it before upgrading')
    manifest, changes = plan(package, game)
    if not apply:
        return {'mode': 'dry-run', 'version': manifest['version'], 'changes': changes}
    if '-' in manifest['version'] and not accept_prerelease:
        raise ValueError('This is a prerelease; use --accept-prerelease for a test installation')
    if game_running():
        raise ValueError('Close NFSU2 before installing')
    state.mkdir(exist_ok=True)
    transaction = uuid.uuid4().hex
    archive = state / transaction
    archive.mkdir()
    journal = {'schema': 1, 'version': manifest['version'], 'transaction': transaction,
               'status': 'installing', 'files': changes}
    for item in changes:
        if item['original_sha256'] is not None:
            copy_atomic(safe_path(game, item['path']), archive / 'original' / item['path'])
    write_json(state / 'state.json', journal)
    try:
        for item in changes:
            target = safe_path(game, item['path'])
            copy_atomic(safe_path(package, item['path']), target)
            if digest(target) != item['installed_sha256']:
                raise OSError('Installed checksum mismatch: ' + item['path'])
    except Exception:
        # Keep a recovery journal and backups even if rollback is interrupted.
        restore(game, journal, recovery=True)
        raise
    journal['status'] = 'installed'
    write_json(state / 'state.json', journal)
    return {'mode': 'installed', 'version': manifest['version'], 'changes': changes}


def restore(game, journal, recovery=False):
    state = state_dir(game)
    transaction = journal['transaction']
    if len(transaction) != 32 or any(c not in '0123456789abcdef' for c in transaction):
        raise ValueError('Invalid transaction identifier')
    archive = state / transaction
    if archive.is_symlink() or not archive.is_dir():
        raise ValueError('Missing or unsafe backup directory')
    checked = []
    for item in journal['files']:
        target = safe_path(game, item['path'])
        before = item['original_sha256']
        if target.exists() and digest(target) not in (item['installed_sha256'], before):
            raise ValueError('File changed since installation; preserve it before uninstalling: ' + item['path'])
        if not target.exists() and not recovery:
            raise ValueError('Installed file missing: ' + item['path'])
        original = safe_path(archive / 'original', item['path'])
        if before is not None and (not original.is_file() or digest(original) != before):
            raise ValueError('Backup checksum mismatch: ' + item['path'])
        checked.append((target, original, item))
    for target, original, item in checked:
        if target.exists():
            saved = archive / 'removed' / item['path']
            saved.parent.mkdir(parents=True, exist_ok=True)
            os.replace(target, saved)
        if item['original_sha256'] is not None:
            copy_atomic(original, target)
    journal['status'] = 'restored'
    write_json(archive / 'journal.json', journal)
    os.replace(state / 'state.json', archive / 'state-restored.json')


def uninstall(game, apply=False):
    state = state_dir(game)
    journal = json.loads((state / 'state.json').read_text())
    if not apply:
        return {'mode': 'dry-run-uninstall', 'files': journal['files']}
    if game_running():
        raise ValueError('Close NFSU2 before uninstalling')
    restore(game, journal, recovery=journal['status'] == 'installing')
    return {'mode': 'restored', 'backups': str(state / journal['transaction'])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--accept-prerelease', action='store_true')
    parser.add_argument('--uninstall', action='store_true')
    args = parser.parse_args()
    game = args.game.resolve(strict=True)
    try:
        result = uninstall(game, args.apply) if args.uninstall else install(args.package.resolve(), game, args.apply, args.accept_prerelease)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
