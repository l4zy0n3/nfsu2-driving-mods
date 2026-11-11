# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
import mmap
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import Mock, patch
import bridge
from ds_core import MAGIC


class MailboxTests(unittest.TestCase):
    def test_connect_resets_only_led_control_before_regular_output(self):
        with tempfile.TemporaryDirectory() as directory:
            entry = Path(directory) / 'hidraw7'
            (entry / 'device').mkdir(parents=True)
            (entry / 'device/uevent').write_text('HID_ID=0003:0000054C:00000CE6\n')
            def ioctl(_fd, operation, data, _mutate):
                if operation == 0x80084803:
                    data[:] = struct.pack('<IHH', 3, 0x054c, 0x0ce6)
            with patch.object(bridge, 'Path') as paths, \
                 patch.object(bridge.os, 'open', return_value=7), \
                 patch.object(bridge.fcntl, 'ioctl', side_effect=ioctl), \
                 patch.object(bridge, 'gyro_calibration', return_value=()), \
                 patch.object(bridge, 'accel_calibration', return_value=()), \
                 patch.object(bridge.os, 'write', return_value=63) as write:
                paths.return_value.glob.return_value = [entry]
                device = bridge.Device()
                self.assertTrue(device.connect())
                reset = bytearray(63)
                reset[0], reset[2] = 2, 8
                write.assert_called_once_with(7, bytes(reset))
                report = bridge.output_report(color=(0, 70, 12))
                device.output(report, 1)
                self.assertEqual(write.call_count, 2)
                write.assert_called_with(7, report)
                self.assertEqual(report[2], 0x14)

    def test_feedback_ownership_follows_transport_and_feature_state(self):
        audio = Mock()
        sample = {'active': True}
        config = bridge.DEFAULTS | {'enabled': True, 'haptics': True, 'speaker': False}
        for running in (True, False):
            audio.tick.return_value = running
            self.assertEqual(bridge.audio_feedback(audio, sample, config, 1), running)
            audio.tick.assert_called_with(sample, 1)
        for override in ({'enabled': False}, {'haptics': False, 'speaker': False}):
            bridge.audio_feedback(audio, sample, config | override, 2)
            audio.tick.assert_called_with(None, 2)
        bridge.audio_feedback(audio, None, config, 3)
        audio.tick.assert_called_with(None, 3)
        bridge.audio_feedback(audio, sample, config | {'haptics': False, 'speaker': True}, 4, True)
        audio.set_gain.assert_called_with(0)
        self.assertEqual(audio.speaker_gain, 0)

    def test_portable_media_commands(self):
        self.assertEqual(bridge.media_command('playPause'), ['playerctl', 'play-pause'])
        self.assertEqual(bridge.media_command('next'), ['playerctl', 'next'])
        self.assertEqual(bridge.media_command('previous'), ['playerctl', 'previous'])
        self.assertIsNone(bridge.media_command('toggle'))
        self.assertIsNone(bridge.media_command('; bad command'))

    def test_demo_exercises_selected_features_in_order(self):
        left, right, clutch = (bridge.demo_sample(t) for t in (1.1, 4.1, 7))
        self.assertGreater(left['load_fl'], left['load_fr'])
        self.assertGreater(right['load_fr'], right['load_fl'])
        self.assertEqual(left['clutch_load'], 0)
        self.assertGreater(clutch['clutch_load'], .5)
        self.assertGreater(clutch['clutch_mismatch'], 20)
        self.assertEqual(bridge.demo_sample(10)['clutch_load'], 0)

    def test_requires_advancing_untorn_heartbeat_and_expires(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'data'
            data = bytearray(128)
            struct.pack_into('<5Ii', data, 0, MAGIC, 1, 2, 500, 35, 2)
            struct.pack_into('<2f', data, 24, 850, 8000)
            path.write_bytes(data)
            with patch.object(bridge, 'MAILBOX', path):
                reader = bridge.Telemetry()
                self.assertIsNone(reader.read(1))
                self.assertIsNone(reader.read(1.02))
                with path.open('r+b') as file:
                    writer = mmap.mmap(file.fileno(), 128)
                    struct.pack_into('<I', writer, 8, 4)
                    self.assertTrue(reader.read(1.04)['active'])
                    self.assertIsNone(reader.read(1.3))
                    struct.pack_into('<I', writer, 8, 5)
                    self.assertIsNone(reader.read(1.32))
                    struct.pack_into('<I', writer, 8, 6)
                    struct.pack_into('<I', writer, 16, 0)
                    self.assertFalse(reader.read(1.34)['active'])
                    writer.close()
                reader.mapping.close()

    def test_refuses_symlink_and_wrong_size(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'data'; target.write_bytes(bytes(32))
            link = Path(directory) / 'link'; link.symlink_to(target)
            with self.assertRaises(OSError): bridge.open_regular(link, 32)
            with self.assertRaises(RuntimeError): bridge.open_regular(target, 128)

    def test_commands_release_same_inode(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'gyro'
            with patch.object(bridge, 'GYRO', path):
                commands = bridge.Commands()
                inode = path.stat().st_ino
                commands.write(True, True, .5, -.5)
                first = struct.unpack('<4IQff', path.read_bytes())
                self.assertEqual(first[:4], (2, bridge.GYRO_MAGIC, 1, 3))
                commands.write()
                last = struct.unpack('<4IQff', path.read_bytes())
                self.assertEqual(last[3], 0)
                self.assertEqual(last[-2:], (0, 0))
                self.assertEqual(path.stat().st_ino, inode)
                commands.mapping.close()


if __name__ == '__main__': unittest.main()
