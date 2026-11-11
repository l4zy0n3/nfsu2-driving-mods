# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
import math
import struct
import unittest
from ds_motion import Gravity, accel_calibration
from ds_core import DEFAULTS, Gestures, usb_input


class MotionTests(unittest.TestCase):
    def test_factory_calibration_signed_endpoints_and_usb_offsets(self):
        report = bytearray(41); report[0] = 5
        pairs = (8202, -8182, 8195, -8189, 8187, -8197)
        struct.pack_into('<6h', report, 23, *pairs)
        factors = accel_calibration(report)
        for (bias, scale), plus, minus in zip(factors, pairs[::2], pairs[1::2]):
            self.assertAlmostEqual((plus - bias) * scale, 1)
            self.assertAlmostEqual((minus - bias) * scale, -1)
        packet = bytearray(64); packet[0] = 1
        struct.pack_into('<3h', packet, 22, -8192, 400, 32767)
        self.assertEqual(usb_input(packet)['accel'], (-8192, 400, 32767))
        for bad in (bytes(41), report[:-1]):
            with self.assertRaises(ValueError): accel_calibration(bad)
        struct.pack_into('<2h', report, 23, 10, 10)
        with self.assertRaises(ValueError): accel_calibration(report)

    def test_pitch_sign_yaw_and_roll_geometry(self):
        for rate in (-30, 30):
            g = Gravity(); g.update((0, 1, 0), (0, 0, 0), 0)
            for _ in range(100): angle = g.update(None, (rate, 0, 0), .01)
            self.assertAlmostEqual(angle, rate, places=2)
        g = Gravity(); g.update((0, 1, 0), (0, 0, 0), 0)
        self.assertAlmostEqual(g.update(None, (0, 90, 0), .01), 0)
        g = Gravity(); g.update((2**-.5, 2**-.5, 0), (0, 0, 0), 0)
        self.assertLess(g.update(None, (0, 30, 0), .01), -.29)

    def test_translation_freefall_and_large_acceleration_rejected(self):
        for accel in ((0, 0, 0), (0, 3, 0), (0, 1, -.3)):
            g = Gravity(); g.update((0, 1, 0), (0, 0, 0), 0)
            for _ in range(100): angle = g.update(accel, (0, 0, 0), .01)
            self.assertAlmostEqual(angle, 0)
        self.assertIsNone(Gravity().update((0, 0, 0), (0, 0, 0), .01))
        self.assertIsNone(Gravity().update((1, 0, 0), (0, 0, 0), .01))

    def test_small_gyro_drift_corrected(self):
        g = Gravity(); g.update((0, 1, 0), (0, 0, 0), 0)
        for _ in range(1000): angle = g.update((0, 1, 0), (1, 0, 0), .01)
        self.assertLess(abs(angle), 1)  # Gyro-only would accumulate ten degrees.

    def test_relative_touch_gain_and_lifecycle(self):
        base = dict(touching=False, touch_id=0, x=1600, y=500, touch_click=False,
                    gyro=(0, 0, 0), sensor_tick=0, accel_g=(0, 1, 0))
        g = Gestures(); g.update(base, [1] * 3, 0, DEFAULTS, True)
        held = base | dict(touching=True, gyro=(30, 0, 0))
        self.assertEqual(g.update(held | {'sensor_tick': 30000}, [1] * 3, .01, DEFAULTS, True)[2:], (0, 0))
        for i in range(2, 22):
            angle = math.radians((i - 1) * .3)
            out = g.update(held | dict(sensor_tick=i * 30000, accel_g=(0, math.cos(angle), -math.sin(angle))),
                           [1] * 3, i / 100, DEFAULTS, True)
        self.assertGreater(out[3], .2)
        # A missing initial accelerometer reference can join without moving the camera.
        h = Gestures(); h.update(held | {'accel_g': None}, [1] * 3, 0, DEFAULTS, True)
        before = h.update(held | dict(accel_g=None, sensor_tick=30000), [1] * 3, .01, DEFAULTS, True)[3]
        after = h.update(held | dict(gyro=(0, 0, 0), sensor_tick=60000), [1] * 3, .02, DEFAULTS, True)[3]
        self.assertAlmostEqual(before, after)
        self.assertEqual(g.update(held | {'sensor_tick': 100000000}, [1] * 3, 2, DEFAULTS, True)[2:], (0, 0))
        self.assertEqual(g.update(None, [1] * 3, 2.1, DEFAULTS, False), ([], False, 0, 0))
        self.assertIsNone(g.gravity.vector)


if __name__ == '__main__': unittest.main()
