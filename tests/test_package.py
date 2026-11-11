# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import package
from install import PAYLOAD


class PackageTests(unittest.TestCase):
    def test_allowlist_matching_source_and_checksums(self):
        version, files = package.payload(ROOT)
        self.assertEqual(version, (ROOT / 'VERSION').read_text().strip())
        manifest = json.loads(files['manifest.json'])
        self.assertEqual(set(manifest['files']), PAYLOAD)
        for name, expected in manifest['files'].items():
            self.assertEqual(hashlib.sha256(files[name]).hexdigest(), expected)
        for line in files['SHA256SUMS'].decode().splitlines():
            expected, name = line.split('  ', 1)
            self.assertEqual(hashlib.sha256(files[name]).hexdigest(), expected)
        for name in ('src/clutch/analog-clutch.c', 'src/clutch/manual_core.h', 'Makefile', 'LICENSE', 'tools/install.py'):
            self.assertEqual(files['source/' + name], (ROOT / name).read_bytes())
        self.assertFalse(any(Path(name).suffix.lower() in ('.exe', '.dll', '.bin') for name in files))
        self.assertNotIn(b'/home' + b'/', b''.join(data for name, data in files.items() if name.endswith(('.c', '.h', '.py', '.ini'))))

    def test_zip_is_reproducible_and_has_no_traversal(self):
        fixture = ('0.1.0-alpha.1', {'scripts/example.txt': b'fixture', 'LICENSE': b'license'})
        with tempfile.TemporaryDirectory() as directory, patch.object(package, 'payload', return_value=fixture):
            path = package.package(Path(directory)); first = path.read_bytes()
            package.package(Path(directory))
            self.assertEqual(first, path.read_bytes())
            with zipfile.ZipFile(path) as archive:
                self.assertIsNone(archive.testzip())
                self.assertTrue(all('..' not in Path(name).parts and not name.startswith('/') for name in archive.namelist()))


if __name__ == '__main__': unittest.main()
