# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('grind', ROOT / 'tools/grind_sound.py')
grind = importlib.util.module_from_spec(spec); spec.loader.exec_module(grind)


class AssetTests(unittest.TestCase):
    def test_grind_is_reproducible_quiet_mono_pcm(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second = Path(directory) / 'a.wav', Path(directory) / 'b.wav'
            grind.generate(first); grind.generate(second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with wave.open(str(first)) as file:
                self.assertEqual((file.getnchannels(), file.getsampwidth(), file.getframerate()), (1, 2, 22050))
                samples = struct.unpack('<4410h', file.readframes(4410))
                self.assertLessEqual(max(abs(v) for v in samples), 3277)
                self.assertGreater(max(samples), 100)


if __name__ == '__main__': unittest.main()
