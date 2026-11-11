# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
import math
import struct
import unittest
from ds_core import *


class CoreTests(unittest.TestCase):
    def sample(self):
        return dict(rpm=3000, rpm_limit=8000, clutch_press=.5, throttle=.4,
                    speed_mps=20, wheel_slip=.1, clutch_load=.4, clutch_mismatch=25,
                    clutch_kick=.2, gear=3, active=True, manual=True,
                    stalled=False, cranking=False, grinding=False, clutch_trigger=1, throttle_trigger=2)

    def test_trigger_effects_follow_bindings_or_remain_off(self):
        sample = self.sample()
        left, right, *_ = feedback(sample, DEFAULTS, 0)
        self.assertNotEqual(left, OFF)
        self.assertNotEqual(right, OFF)
        swapped = sample | {'clutch_trigger': 2, 'throttle_trigger': 1}
        self.assertEqual(feedback(swapped, DEFAULTS, 0)[:2], (right, left))
        for routes in ({'clutch_trigger': 0, 'throttle_trigger': 0},
                       {'clutch_trigger': None, 'throttle_trigger': None}):
            self.assertEqual(feedback(sample | routes, DEFAULTS, 0)[:2], (OFF, OFF))

    def test_telemetry_trigger_routes_and_invalid_collisions(self):
        data = bytearray(128)
        for clutch, throttle in ((0, 0), (1, 2), (2, 1), (0, 2), (1, 0)):
            struct.pack_into('<6I', data, 0, MAGIC, 1, 2, 100, 3 | (clutch << 6) | (throttle << 8), 2)
            decoded = telemetry(data)
            self.assertEqual((decoded['clutch_trigger'], decoded['throttle_trigger']), (clutch, throttle))
        for flags in (3 | (3 << 6), 3 | (3 << 8), 3 | (1 << 6) | (1 << 8), 1024):
            struct.pack_into('<I', data, 16, flags)
            self.assertIsNone(telemetry(data))

    def test_trigger_encoding(self):
        data = effect([0, 1, 8] + [0] * 7)
        self.assertEqual(data, bytes([0x21, 6, 0, 0xc0, 1, 0, 0, 0, 0, 0, 0]))
        self.assertEqual(effect([0] * 10), OFF)
        for zones in ([1], [9] * 10, [-1] * 10):
            with self.assertRaises(ValueError): effect(zones)
        self.assertEqual(effect([2] * 10, 28)[9], 28)

    def test_output_only_expected_usb_fields(self):
        data = output_report(effect([1] * 10), OFF, (4, 5, 6), 7, True)
        self.assertEqual(len(data), 63)
        self.assertEqual(data[0], 2)
        self.assertEqual(data[1] & 3, 0)  # No compatible rumble / PCM override.
        self.assertEqual(data[1] & 0xe0, 0)  # Native audio routing/volumes remain unchanged.
        self.assertEqual(data[11:22], OFF)
        self.assertEqual(data[22:33], effect([1] * 10))
        self.assertEqual(data[45:48], bytes([4, 5, 6]))
        self.assertEqual(data[2], 0x14)  # RGB and player LEDs, no repeated reset.
        self.assertEqual(data[39:44], bytes(5))  # No built-in lightbar animation/setup.
        self.assertEqual(data[7], 0)  # Microphone not enabled by the bridge.

    def test_stale_inactive_disable_and_manual(self):
        s = self.sample()
        for sample in (None, s | {'active': False}):
            self.assertEqual(feedback(sample, DEFAULTS, 0), (OFF, OFF, (0, 0, 0), 0))
        self.assertEqual(feedback(s, DEFAULTS | {'enabled': False}, 0)[0], OFF)
        self.assertEqual(feedback(s | {'manual': False}, DEFAULTS, 0)[0], OFF)
        self.assertNotEqual(feedback(s | {'stalled': True}, DEFAULTS, 0)[0], OFF)
        self.assertEqual(feedback(s, DEFAULTS | {'trigger_gain': 0}, 0)[:2], (OFF, OFF))

    def test_revlimit_and_slip_trigger(self):
        s = self.sample()
        self.assertEqual(feedback(s, DEFAULTS, 0)[1][0], 0x21)
        self.assertEqual(feedback(s | {'wheel_slip': .9}, DEFAULTS, 0)[1][0], 0x26)
        self.assertEqual(feedback(s | {'rpm': 7900}, DEFAULTS, 0)[1][9], 28)

    def test_car_bite_matches_release_and_full_press_remains_free(self):
        s = self.sample() | dict(bite_start=.2, bite_end=.4, engine_inertia=.0003, clutch_capacity=1.25)
        profile = feedback(s, DEFAULTS | {'trigger_gain': 1}, 0)[0]
        active, packed = struct.unpack_from('<HI', profile, 1)
        forces = [((packed >> (3 * i)) & 7) + 1 if active & (1 << i) else 0 for i in range(10)]
        self.assertEqual(forces.index(max(forces)), 7)  # Release .3 means press .7.
        self.assertEqual(forces[9], 0)
        lighter = feedback(s | {'engine_inertia': .00015}, DEFAULTS, 0)[0]
        self.assertNotEqual(lighter, feedback(s | {'engine_inertia': .0006}, DEFAULTS, 0)[0])

    def test_shift_light_follows_live_build_redline_not_gear_recommendation(self):
        s = self.sample() | dict(shift_light_rpm=6000, idle_rpm=850, rpm_limit=9500)
        self.assertEqual(feedback(s | {'rpm': 6001}, DEFAULTS, .125)[2], (0, 70, 12))
        self.assertEqual(feedback(s | {'rpm': 9499}, DEFAULTS, .125)[2], (110, 5, 0))
        self.assertEqual(feedback(s | {'rpm': 9500}, DEFAULTS, .125)[2:], ((65, 35, 140), 31))
        self.assertEqual(feedback(s | {'rpm': 9500}, DEFAULTS, 0)[3], 0)
        self.assertEqual(feedback(s | {'stalled': True}, DEFAULTS, 0)[3], 0)
        # Same car, changed engine build: follow the new live limit immediately.
        self.assertEqual(feedback(s | {'rpm': 7000, 'rpm_limit': 7000}, DEFAULTS, .125)[2:], ((65, 35, 140), 31))
        self.assertEqual(feedback(s | {'rpm': 7000, 'rpm_limit': 9000}, DEFAULTS, .125)[2], (100, 45, 0))

    def test_clutch_breakover_judder_and_release_gates(self):
        s = self.sample() | dict(bite_start=.2, bite_end=.8, engine_inertia=.0003,
                               clutch_capacity=1.25, clutch_load=0)
        profile = feedback(s, DEFAULTS | {'trigger_gain': 1}, 0)[0]
        active, packed = struct.unpack_from('<HI', profile, 1)
        forces = [((packed >> (3*i)) & 7) + 1 if active & (1 << i) else 0 for i in range(10)]
        self.assertGreater(forces[4], forces[6])  # Faster force drop beyond the bite peak.
        loaded = s | dict(clutch_load=.8, clutch_mismatch=100)
        vibration = feedback(loaded, DEFAULTS, 0)[0]
        self.assertEqual(vibration[0], 0x26)
        self.assertGreater(vibration[9], 12)
        self.assertEqual(struct.unpack_from('<H', vibration, 1)[0] & (1 << 9), 0)
        for change in ({'clutch_press': 1}, {'clutch_press': 0}, {'clutch_load': 0},
                       {'clutch_mismatch': 0}, {'stalled': True}):
            self.assertEqual(feedback(loaded | change, DEFAULTS, 0)[0][0], 0x21)
        self.assertEqual(feedback(loaded, DEFAULTS | {'trigger_gain': 0}, 0)[0], OFF)

    def test_wheel_protocol_extension_is_optional_and_bounded(self):
        data = bytearray(128)
        struct.pack_into('<5Ii', data, 0, MAGIC, 1, 2, 500, 35, 3)
        struct.pack_into('<2f', data, 24, 3000, 8000)
        self.assertFalse(telemetry(data)['wheel_data_valid'])
        struct.pack_into('<4f', data, 60, .7, .4, .6, .3)
        struct.pack_into('<I', data, 80, 123)
        struct.pack_into('<I2f', data, 116, 2, .8, .2)
        parsed = telemetry(data)
        self.assertTrue(parsed['wheel_data_valid'])
        self.assertEqual(parsed['physics_sequence'], 123)
        self.assertAlmostEqual(parsed['slip_left'], .8)
        self.assertAlmostEqual(parsed['load_rl'], .6)
        self.assertNotIn('car_type', parsed)
        for offset, value in ((60, -1), (72, 1.1), (120, math.nan), (124, 2)):
            bad = bytearray(data); struct.pack_into('<f', bad, offset, value)
            self.assertIsNone(telemetry(bad))
        struct.pack_into('<I', data, 116, 4)
        self.assertIsNone(telemetry(data))

    def test_telemetry_boundaries(self):
        data = bytearray(128)
        struct.pack_into('<5Ii', data, 0, MAGIC, 1, 2, 500, 35, 3)
        struct.pack_into('<9f', data, 24, 3000, 8000, .5, .4, 20, .1, .4, 25, .2)
        self.assertTrue(telemetry(data)['active'])
        self.assertIsNone(telemetry(data[:-1]))
        for offset, value in ((0, 0), (4, 2), (8, 3), (16, 1024), (20, 100)):
            bad = bytearray(data); struct.pack_into('<I', bad, offset, value)
            self.assertIsNone(telemetry(bad))
        for offset in range(24, 80, 4):
            bad = bytearray(data);struct.pack_into('<f', bad, offset, math.nan)
            self.assertIsNone(telemetry(bad))
        struct.pack_into('<I7fI', data, 84, 15, .2, .6, 850, .0003, 1.25, 7400, .3, 1)
        self.assertEqual(telemetry(data)['car_type'], 15)
        self.assertEqual(telemetry(data)['shift_light_rpm'], 7400)
        for offset in range(88, 116, 4):
            bad = bytearray(data);struct.pack_into('<f', bad, offset, math.nan)
            self.assertIsNone(telemetry(bad))
        bad = bytearray(data);struct.pack_into('<f', bad, 92, .1)
        self.assertIsNone(telemetry(bad))

    def test_input_and_calibration(self):
        packet = bytearray(64);packet[0] = 1;packet[5] = 255;packet[33] = 128
        struct.pack_into('<3hI', packet, 16, 11, -22, 33, 0)
        self.assertEqual(usb_input(packet)['gyro'], (11, -22, 33))
        self.assertFalse(usb_input(packet)['touching'])
        self.assertEqual(usb_input(packet)['l2'], 1)
        self.assertIsNone(usb_input(packet[:-1]))
        calibration = bytearray(41);calibration[0] = 5
        struct.pack_into('<11h', calibration, 1, 0, 0, 0, 10000, -10000, 10000, -10000, 10000, -10000, 540, 540)
        self.assertEqual(gyro_calibration(calibration), [.054] * 3)
        calibration[7:19] = bytes(12)
        with self.assertRaises(ValueError): gyro_calibration(calibration)

    def test_gestures_release_uses_last_valid_coordinates(self):
        g = Gestures()
        base = dict(touching=True, touch_id=0, x=500, y=500, touch_click=False,
                    gyro=(0, 0, 0), sensor_tick=0)
        self.assertEqual(g.update(base, [.05] * 3, 1, DEFAULTS, True)[0], [])
        # Released coordinates are invalid and must not turn a tap into a swipe.
        out = g.update(base | {'touching': False, 'x': 1900}, [.05] * 3, 1.1, DEFAULTS, True)
        self.assertEqual(out[0], ['playPause'])
        g.update(base, [.05] * 3, 2, DEFAULTS, True)
        g.update(base | {'x': 900}, [.05] * 3, 2.1, DEFAULTS, True)
        self.assertEqual(g.update(base | {'touching': False}, [.05] * 3, 2.2, DEFAULTS, True)[0], ['next'])
        g.update(base, [.05] * 3, 3, DEFAULTS, True)
        self.assertEqual(g.update(base | {'touching': False}, [.05] * 3, 3.8, DEFAULTS, True)[0], ['toggle'])

    def test_gyro_held_only_right_zone_and_focus(self):
        g = Gestures()
        base = dict(touching=True, touch_id=0, x=1600, y=500, touch_click=False,
                    gyro=(100, -200, 0), sensor_tick=0)
        self.assertTrue(g.update(base, [.05] * 3, 1, DEFAULTS, True)[1])
        out = g.update(base | {'sensor_tick': 60000}, [.05] * 3, 1.02, DEFAULTS, True)
        self.assertGreater(out[2], 0)
        g.update(base | {'sensor_tick': 120000}, [.05] * 3, 1.04, DEFAULTS | {'gyro': False}, True)
        self.assertEqual((g.yaw, g.pitch), (0, 0))
        g.update(None, [.05] * 3, 1.3, DEFAULTS, False)
        self.assertIsNone(g.start)
        self.assertIsNone(g.sensor_tick)
        self.assertEqual(g.update(base, [.05] * 3, 1.32, DEFAULTS, True)[2:], (0, 0))
        self.assertEqual(g.update(base, [.05] * 3, 1.03, DEFAULTS, False), ([], False, 0, 0))
        self.assertFalse(g.update(base, None, 1.04, DEFAULTS, True)[1])

    def test_config_validation(self):
        self.assertEqual(config_checked({}), DEFAULTS)
        for values in ({'speaker_gain': 1}, {'gyro_gain': math.nan}, {'enabled': 1}, {'unknown': 0}):
            with self.assertRaises(ValueError): config_checked(values)


if __name__ == '__main__':
    unittest.main()
