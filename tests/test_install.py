# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('installer', ROOT / 'tools/install.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.package, self.game = root / 'package', root / 'game'
        self.package.mkdir(); self.game.mkdir()
        required = {}
        for name in ('SPEED2.EXE', 'dinput8.dll'):
            path = self.game / name
            path.write_bytes(('synthetic fixture ' + name).encode())
            required[name] = installer.digest(path)
        (self.package / 'compatibility.json').write_text(json.dumps({'required_files': required}))
        files = {}
        for name in installer.PAYLOAD:
            path = self.package / name
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(('package ' + name).encode())
            files[name] = installer.digest(path)
        (self.package / 'manifest.json').write_text(json.dumps({'schema': 1, 'version': '0.1.0-alpha.1', 'files': files}))
        patcher = patch.object(installer, 'game_running', return_value=False)
        patcher.start(); self.addCleanup(patcher.stop)

    def install(self):
        return installer.install(self.package, self.game, True, True)

    def test_dry_run_changes_nothing(self):
        before = sorted(self.game.rglob('*'))
        result = installer.install(self.package, self.game)
        self.assertEqual(result['mode'], 'dry-run')
        self.assertEqual(before, sorted(self.game.rglob('*')))

    def test_restore_roundtrip_keeps_config_saves_and_original_mod(self):
        scripts = self.game / 'scripts'; scripts.mkdir()
        old = scripts / 'NFSU2.AnalogClutch.asi'; old.write_bytes(b'old mod')
        config = scripts / 'NFSU2.Realism.ini'; config.write_bytes(b'personal controls')
        save = self.game / 'career'; save.write_bytes(b'career progress')
        result = self.install()
        self.assertEqual(result['mode'], 'installed')
        self.assertNotEqual(old.read_bytes(), b'old mod')
        self.assertEqual(config.read_bytes(), b'personal controls')
        installer.uninstall(self.game, True)
        self.assertEqual(old.read_bytes(), b'old mod')
        self.assertEqual(config.read_bytes(), b'personal controls')
        self.assertEqual(save.read_bytes(), b'career progress')
        self.assertFalse((scripts / 'NFSU2.MirrorPanels.asi').exists())

    def test_prerelease_and_running_game_require_no_writes(self):
        with self.assertRaises(ValueError): installer.install(self.package, self.game, True)
        with patch.object(installer, 'game_running', return_value=True):
            with self.assertRaises(ValueError): self.install()
        self.assertFalse((self.game / installer.STATE).exists())

    def test_rejects_dependency_mismatch_corruption_and_symlink(self):
        exe = self.game / 'SPEED2.EXE'; original = exe.read_bytes(); exe.write_bytes(b'wrong')
        with self.assertRaises(ValueError): self.install()
        exe.write_bytes(original)
        payload = self.package / 'scripts/NFSU2.AnalogClutch.asi'
        original = payload.read_bytes(); payload.write_bytes(b'corrupt')
        with self.assertRaises(ValueError): self.install()
        payload.write_bytes(original)
        (self.game / 'scripts').symlink_to(self.package / 'scripts', target_is_directory=True)
        with self.assertRaises(ValueError): self.install()

    def test_rejects_traversal_manifest(self):
        path = self.package / 'manifest.json'; manifest = json.loads(path.read_text())
        manifest['files']['../outside'] = 'bad'
        path.write_text(json.dumps(manifest))
        with self.assertRaises(ValueError): self.install()

    def test_modified_install_and_corrupt_backup_prevent_restore(self):
        old = self.game / 'scripts/NFSU2.AnalogClutch.asi'; old.parent.mkdir(); old.write_bytes(b'original')
        self.install()
        installed = old.read_bytes(); old.write_bytes(b'user change')
        with self.assertRaises(ValueError): installer.uninstall(self.game, True)
        self.assertEqual(old.read_bytes(), b'user change')
        old.write_bytes(installed)
        state = self.game / installer.STATE
        journal = json.loads((state / 'state.json').read_text())
        backup = state / journal['transaction'] / 'original/scripts/NFSU2.AnalogClutch.asi'
        backup.write_bytes(b'corrupt backup')
        with self.assertRaises(ValueError): installer.uninstall(self.game, True)
        self.assertEqual(old.read_bytes(), installed)

    def test_mid_install_failure_rolls_back(self):
        real_copy = installer.copy_atomic
        count = 0
        def failing_copy(source, target):
            nonlocal count
            if source.is_relative_to(self.package):
                count += 1
                if count == 2: raise OSError('simulated disk error')
            return real_copy(source, target)
        with patch.object(installer, 'copy_atomic', side_effect=failing_copy):
            with self.assertRaises(OSError): self.install()
        self.assertFalse(any((self.game / 'scripts').glob('*')))
        self.assertFalse((self.game / installer.STATE / 'state.json').exists())


if __name__ == '__main__':
    unittest.main()
