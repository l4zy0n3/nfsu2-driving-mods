# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
"""Offline tests only. No pw-cat process, audio device, or HID is opened."""
import json
import os
import subprocess
import unittest
from unittest.mock import patch

import numpy as np

import ds_audio as ds


def graph():
    return [
        {'id': 10, 'type': 'PipeWire:Interface:Device', 'info': {'props': {
            'device.vendor.id': '0x054c', 'device.product.id': '0x0ce6'}}},
        {'id': 20, 'type': 'PipeWire:Interface:Node', 'info': {'props': {
            'device.id': 10, 'media.class': 'Audio/Sink', 'audio.channels': 4,
            'audio.position': '[ FL, FR, RL, RR ]'}}},
    ]


class TargetTests(unittest.TestCase):
    def test_requires_identity_channel_order_and_unique_device(self):
        self.assertEqual(ds.audio_target(graph()), '20')
        for key, value in (('audio.channels', 2), ('audio.position', '[ RL RR FL FR ]'), ('device.id', 99)):
            nodes = graph()
            nodes[1]['info']['props'][key] = value
            with self.assertRaises(ValueError): ds.audio_target(nodes)
        nodes = graph()
        nodes[0]['info']['props']['device.product.id'] = '0x1234'
        with self.assertRaises(ValueError): ds.audio_target(nodes)
        with self.assertRaises(ValueError): ds.audio_target(graph() + graph())
        with self.assertRaises(ValueError): ds.audio_target([])


def sample(**values):
    return {"active": True, "gear": 2, "stalled": False, **values}


class RendererTests(unittest.TestCase):
    def setUp(self):
        self.no_playback = patch.object(ds.subprocess, "Popen", side_effect=AssertionError("hardware forbidden"))
        self.no_playback.start()
        self.addCleanup(self.no_playback.stop)
        self.audio = ds.AudioEffects()

    def test_pcm_format_and_engine_channel_isolation(self):
        pcm = self.audio.render(sample(rpm=4000, throttle=0.6), 0)
        self.assertEqual(pcm.shape, (960, 4))
        self.assertEqual(pcm.dtype, np.dtype("<i2"))
        self.assertEqual(len(pcm.tobytes()), 7680)
        self.assertFalse(pcm[:, :2].any())
        self.assertTrue(pcm[:, 2].any())
        self.assertTrue(pcm[:, 3].any())
        self.assertFalse(np.array_equal(pcm[:, 2], pcm[:, 3]))

    def test_mechanical_effects_are_distinct_and_do_not_feed_speaker(self):
        effects = [dict(wheel_slip=0.8), dict(clutch_load=1, clutch_mismatch=160),
                   dict(clutch_kick=1), dict(grinding=True)]
        outputs = []
        for effect in effects:
            pcm = ds.AudioEffects().render(sample(**effect), 0)
            self.assertFalse(pcm[:, :2].any())
            self.assertTrue(pcm[:, 2:].any())
            self.assertFalse(np.array_equal(pcm[:, 2], pcm[:, 3]))
            outputs.append(pcm)
        for previous, current in zip(outputs, outputs[1:]):
            self.assertFalse(np.array_equal(previous, current))

    def test_judder_requires_load_and_engaged_clutch_and_uses_rad_s(self):
        values = dict(clutch_load=1, clutch_mismatch=160)
        positive = ds.AudioEffects().render(sample(**values), 0)
        negative = ds.AudioEffects().render(sample(clutch_load=1, clutch_mismatch=-160), 0)
        self.assertTrue(np.array_equal(positive, negative))
        self.assertFalse(ds.AudioEffects().render(sample(**values, clutch_press=1), 0).any())
        self.assertFalse(ds.AudioEffects().render(sample(clutch_mismatch=160), 0).any())
        half = ds.AudioEffects().render(sample(clutch_load=1, clutch_mismatch=80), 0)
        self.assertLess(np.max(np.abs(half)), np.max(np.abs(positive)))

    def test_none_and_inactive_clear_event_tails_and_restart_without_false_shift(self):
        self.audio.render(sample(gear=2), 0)
        self.assertTrue(self.audio.render(sample(gear=3), 0.02)[:, :2].any())
        for invalid in (None, {}, sample(active=False)):
            self.assertFalse(self.audio.render(invalid, 0.04).any())
            self.assertFalse(self.audio.render(sample(gear=4, stalled=True), 0.06).any())

    def test_gear_event_does_not_repeat_on_held_telemetry(self):
        self.assertFalse(self.audio.render(sample(gear=2), 0)[:, :2].any())
        self.assertTrue(self.audio.render(sample(gear=3), 0.02)[:, :2].any())
        for frame in range(2, 10):
            pcm = self.audio.render(sample(gear=3), frame / 50)
        self.assertFalse(pcm[:, :2].any())
        self.assertTrue(self.audio.render(sample(gear=2), 0.20)[:, :2].any())

    def test_crank_and_stall_events_only_on_transitions(self):
        for field in ("cranking", "stalled"):
            audio = ds.AudioEffects()
            audio.render(sample(), 0)
            self.assertTrue(audio.render(sample(**{field: True}), 0.02)[:, :2].any())
            for frame in range(2, 30):
                pcm = audio.render(sample(**{field: True}), frame / 50)
            self.assertFalse(pcm[:, :2].any())
            audio.render(sample(), 0.6)
            self.assertTrue(audio.render(sample(**{field: True}), 0.62)[:, :2].any())

    def test_kick_edge_is_not_retriggered_by_repeated_publisher_sample(self):
        first = self.audio.render(sample(clutch_kick=1), 0)
        self.assertTrue(first[:, 2:].any())
        for frame in range(1, 10):
            pcm = self.audio.render(sample(clutch_kick=1), frame / 50)
        self.assertFalse(pcm.any())
        self.audio.render(sample(clutch_kick=0), 0.2)
        self.assertTrue(self.audio.render(sample(clutch_kick=1), 0.22)[:, 2:].any())

    def test_clock_jump_discards_stale_transition_history(self):
        for new_time in (2, -1):
            audio = ds.AudioEffects()
            audio.render(sample(gear=2), 0)
            pcm = audio.render(sample(gear=3, stalled=True), new_time)
            self.assertFalse(pcm.any())

    def test_all_sample_values_finite_clamped_and_peak_bounded(self):
        intense = sample(rpm=30000, rpm_limit=500, throttle=2, speed_mps=1000,
                         wheel_slip=100, clutch_load=5, clutch_mismatch=1e6,
                         clutch_kick=3, grinding=True, cranking=True)
        for gain in (0.5, 1, 9):
            audio = ds.AudioEffects(gain=gain, speaker_gain=1)
            for frame in range(50):
                pcm = audio.render({**intense, "gear": frame % 8}, frame / 50)
                self.assertLessEqual(np.max(np.abs(pcm[:, 2:].astype(int))), round(ds.HAPTIC_PEAK * min(gain, 1) * 32767))
                self.assertLessEqual(np.max(np.abs(pcm[:, :2].astype(int))), round(0.12 * 32767))
        keys = ("rpm", "rpm_limit", "clutch_press", "throttle", "speed_mps",
                "wheel_slip", "clutch_load", "clutch_mismatch", "clutch_kick",
                "gear", "stalled", "cranking", "grinding")
        for invalid in (None, float("nan"), float("inf"), -float("inf"), [], {}, "bad", 10**1000):
            pcm = self.audio.render({"active": True, **dict.fromkeys(keys, invalid)}, 0)
            self.assertFalse(pcm.any())
        for bad_time in (None, float("nan"), float("inf"), "bad"):
            self.assertFalse(self.audio.render(intense, bad_time).any())

    def test_set_gain_controls_haptics_without_speaker_gain_change(self):
        self.audio.set_gain(0)
        self.audio.render(sample(gear=2), 0)
        pcm = self.audio.render(sample(gear=3, rpm=5000), 0.02)
        self.assertFalse(pcm[:, 2:].any())
        self.assertTrue(pcm[:, :2].any())
        self.audio.set_gain(float("nan"))
        self.assertEqual(self.audio.gain, 0)
        self.audio.set_gain(5)
        self.assertEqual(self.audio.gain, 1)

    def test_each_wheel_bump_reaches_only_its_side(self):
        base = sample(wheel_data_valid=True, physics_sequence=1, car_type=15,
                      speed_mps=20, load_fl=.5, load_fr=.5, load_rl=.5, load_rr=.5)
        for key, channel in (('load_fl', 2), ('load_fr', 3), ('load_rl', 2), ('load_rr', 3)):
            audio = ds.AudioEffects()
            self.assertFalse(audio.render(base, 0).any())
            bump = base | {key: .9, 'physics_sequence': 2}
            pcm = audio.render(bump, .02)
            self.assertTrue(pcm[:, channel].any())
            self.assertFalse(pcm[:, 5 - channel].any())
            self.assertFalse(pcm[:, :2].any())
            level = audio._road_level[channel - 2]
            audio.render(bump, .04)
            self.assertLess(audio._road_level[channel - 2], level)  # Repeated telemetry does not retrigger.

    def test_road_history_reset_no_stationary_or_car_change_pulse(self):
        base = sample(wheel_data_valid=True, physics_sequence=1, car_type=15,
                      speed_mps=20, load_fl=.5, load_fr=.5, load_rl=.5, load_rr=.5)
        for change in ({'speed_mps': 0}, {'car_type': 17}):
            audio = ds.AudioEffects(); audio.render(base, 0)
            self.assertFalse(audio.render(base | {'load_fl': 1, 'physics_sequence': 2} | change, .02).any())
        self.audio.render(base, 0)
        self.audio.render(sample(), .02)
        self.assertFalse(self.audio.render(base | {'load_fl': 1, 'physics_sequence': 3}, .04).any())
        self.audio.render(None, .06)
        self.assertFalse(self.audio.render(base | {'load_fl': 1, 'physics_sequence': 4}, .08).any())

    def test_measured_slip_side_and_landing(self):
        for side, channel in (('slip_left', 2), ('slip_right', 3)):
            pcm = ds.AudioEffects().render(sample(wheel_data_valid=True, **{side: .8}), 0)
            self.assertTrue(pcm[:, channel].any())
            self.assertFalse(pcm[:, 5 - channel].any())
        airborne = sample(wheel_data_valid=True, physics_sequence=1, speed_mps=20,
                          load_fl=0, load_fr=0, load_rl=0, load_rr=0)
        self.assertFalse(self.audio.render(airborne, 0).any())
        landing = airborne | dict(physics_sequence=2, load_fl=.7, load_rl=.6)
        pcm = self.audio.render(landing, .02)
        self.assertTrue(pcm[:, 2].any())
        self.assertFalse(pcm[:, 3].any())


class PipeChild:
    """Real local pipe, fake process. Never starts pw-cat or opens audio."""
    def __init__(self):
        self.reader, writer = os.pipe()
        os.set_blocking(self.reader, False)
        self.stdin = os.fdopen(writer, "wb", buffering=0)
        self.exited = None
        self.actions = []

    def poll(self):
        return self.exited

    def wait(self, timeout):
        self.actions.append(("wait", timeout))
        self.exited = 0
        return 0

    def terminate(self):
        self.actions.append(("terminate",))

    def kill(self):
        self.actions.append(("kill",))

    def read(self):
        chunks = []
        while True:
            try:
                block = os.read(self.reader, 16384)
                if not block:
                    break
                chunks.append(block)
            except BlockingIOError:
                break
        return b"".join(chunks)

    def cleanup(self):
        self.stdin.close()
        os.close(self.reader)


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.child = PipeChild()
        self.addCleanup(self.child.cleanup)
        self.popen = patch.object(ds.subprocess, "Popen", return_value=self.child).start()
        patch.object(ds.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, json.dumps(graph()))).start()
        self.addCleanup(patch.stopall)
        self.audio = ds.AudioEffects()
        self.addCleanup(self.audio.close)

    def test_exact_target_format_and_no_fallback(self):
        self.assertTrue(self.audio.tick(sample(rpm=3000), 0))
        command = self.popen.call_args.args[0]
        for option, expected in (("--target", '20'), ("--rate", "48000"),
                                 ("--channels", "4"), ("--channel-map", "FL,FR,RL,RR"),
                                 ("--format", "s16")):
            self.assertEqual(command[command.index(option) + 1], expected)
        properties = json.loads(command[command.index("--properties") + 1])
        for key in ("node.dont-fallback", "node.dont-reconnect", "stream.dont-remix"):
            self.assertIs(properties[key], True)
        self.assertIn("--raw", command)
        self.assertEqual(len(self.child.read()), len(ds.SILENCE))

    def test_inactive_never_starts_output(self):
        self.assertFalse(self.audio.tick(None, 0))
        self.assertFalse(self.audio.tick(sample(active=False), 0.02))
        self.assertFalse(self.audio.tick(sample(), float("nan")))
        self.popen.assert_not_called()

    def test_process_persistent_and_blocked_pipe_drops_without_backlog(self):
        self.audio.tick(sample(rpm=3000), 0)
        for frame in range(1, 100):
            self.audio.tick(sample(rpm=4000), frame / 50)
        self.popen.assert_called_once()
        self.assertGreater(self.audio.dropped_blocks, 0)
        self.assertLessEqual(len(self.child.read()), 8192)
        self.assertTrue(self.audio.tick(sample(), 2))
        self.assertEqual(len(self.child.read()), 7680)

    def test_none_writes_silence_closes_only_our_child(self):
        self.audio.tick(sample(rpm=3000), 0)
        self.child.read()
        self.assertFalse(self.audio.tick(None, 0.02))
        self.assertEqual(self.child.read(), ds.SILENCE)
        self.assertTrue(self.child.stdin.closed)
        self.assertIsNone(self.audio._process)
        self.assertEqual(self.child.actions, [("wait", 0.08)])

    def test_failed_start_is_rate_limited_and_does_not_use_other_target(self):
        self.popen.side_effect = FileNotFoundError("pw-cat unavailable")
        self.assertFalse(self.audio.tick(sample(), 0))
        self.assertFalse(self.audio.tick(sample(), 0.02))
        self.assertEqual(self.popen.call_count, 1)
        self.assertIn("unavailable", self.audio.last_error)
        self.assertFalse(self.audio.tick(sample(), 1))
        self.assertEqual(self.popen.call_count, 2)

    def test_broken_pipe_fails_closed(self):
        self.audio.tick(sample(), 0)
        self.child.read()
        with patch.object(ds.os, "write", side_effect=BrokenPipeError("disconnected")):
            self.assertFalse(self.audio.tick(sample(rpm=4000), 0.02))
        self.assertIsNone(self.audio._process)
        self.assertIn("disconnected", self.audio.last_error)

    def test_shutdown_escalates_boundedly_for_stuck_own_child(self):
        self.audio.tick(sample(), 0)
        self.child.read()
        with patch.object(self.child, "wait", side_effect=subprocess.TimeoutExpired("fake", 0.08)) as wait:
            self.audio.close()
        self.assertEqual([call.kwargs["timeout"] for call in wait.call_args_list], [0.08, 0.02, 0.02])
        self.assertEqual(self.child.actions, [("terminate",), ("kill",)])


if __name__ == "__main__":
    unittest.main()
