# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
"""Procedural DualSense effects. No HID writes, audio capture, or stream rerouting.

tick() accepts 50 Hz samples (25 Hz publishers may repeat their latest sample).
rpm/rpm_limit are RPM, speed_mps is signed m/s, clutch_mismatch is rad/s;
clutch_press/throttle/wheel_slip/clutch_load/clutch_kick are normalized 0..1.
None or inactive samples silence and close only our pw-cat child. The caller
owns freshness checks and must pass None for stale data. render() is pure PCM
generation with state, and never opens audio: use it for offline tests.
"""
import fcntl
import json
import math
import os
import select
import subprocess
import time

import numpy as np

RATE = 48000
FRAMES = RATE // 50
CHANNELS = 4  # FL, FR: speaker cues; RL, RR: independent haptic waveforms.
HAPTIC_PEAK = 0.36
QUANTUM_BYTES = 480 * CHANNELS * 2  # 10 ms, atomic on Linux pipes.
SILENCE = bytes(FRAMES * CHANNELS * 2)


def audio_target(graph):
    """Require one four-channel USB DualSense sink; never select the default sink."""
    devices = set()
    for item in graph:
        props = item.get('info', {}).get('props', {})
        try:
            vendor = int(str(props.get('device.vendor.id', '0')), 16)
            product = int(str(props.get('device.product.id', '0')), 16)
        except ValueError:
            continue
        if item.get('type') == 'PipeWire:Interface:Device' and vendor == 0x054c and product == 0x0ce6:
            devices.add(str(item['id']))
    candidates = []
    for item in graph:
        props = item.get('info', {}).get('props', {})
        positions = props.get('audio.position', '')
        if isinstance(positions, str):
            positions = positions.strip('[] ').replace(',', ' ').replace('"', '').split()
        if (item.get('type') == 'PipeWire:Interface:Node' and
                str(props.get('device.id')) in devices and props.get('media.class') == 'Audio/Sink' and
                str(props.get('audio.channels')) == '4' and positions == ['FL', 'FR', 'RL', 'RR']):
            candidates.append(str(item['id']))
    if len(candidates) != 1:
        raise ValueError('Need exactly one four-channel USB DualSense sink (FL FR RL RR)')
    return candidates[0]


def _number(value, low, high, default=0.0):
    try:
        value = float(value)
    except (TypeError, ValueError, OverflowError):
        return default
    return min(high, max(low, value)) if math.isfinite(value) else default


def _flag(sample, key):
    return _number(sample.get(key, 0), 0, 1) >= 0.5


def _gear(sample):
    value = _number(sample.get("gear"), -1, 10, -1)
    return int(value) if float(value).is_integer() and 0 <= value <= 9 else None


class AudioEffects:
    """One owner thread; tick/render/set_gain/close must not run concurrently.

    The target must match USB identity and four-channel order. Blocking output drops new 10 ms blocks rather
    than building a stale queue. close() waits at most about 150 ms for this
    class's child; hardware audibility and tactile strength need calibration.
    last_error and dropped_blocks expose failures without logging every tick.
    """
    def __init__(self, gain=0.5, speaker_gain=0.08):
        self.set_gain(gain)
        self.speaker_gain = _number(speaker_gain, 0, 0.12)
        self.dropped_blocks = 0
        self.last_error = None
        self._process = None
        self._retry_at = 0.0
        self._reset()

    def set_gain(self, gain):
        self.gain = _number(gain, 0, 1)

    def _reset(self):
        self._phase = {}
        self._events = []
        self._kick_age = 1.0
        self._kick_level = 0.0
        self._last_kick = 0.0
        self._last_gear = None
        self._last_stalled = None
        self._last_cranking = False
        self._last_now = None
        self._attack = 0.0
        self._road_loads = None
        self._road_sequence = None
        self._road_car = None
        self._road_level = [0.0, 0.0]

    def _wave(self, name, hz):
        phase = self._phase.get(name, 0.0)
        step = 2 * math.pi * hz / RATE
        values = np.sin(phase + np.arange(FRAMES) * step)
        self._phase[name] = (phase + FRAMES * step) % (2 * math.pi)
        return values

    def render(self, sample, now):
        """Return (960,4) little-endian signed16 interleaved PCM; no device I/O."""
        now = _number(now, -1e20, 1e20, math.nan)
        if not isinstance(sample, dict) or not _flag(sample, "active") or not math.isfinite(now):
            self._reset()
            return np.zeros((FRAMES, CHANNELS), dtype="<i2")
        if self._last_now is not None and (now < self._last_now or now - self._last_now > 0.25):
            self._reset()
        self._last_now = now
        rpm = _number(sample.get("rpm"), 0, 30000)
        limit = _number(sample.get("rpm_limit", 8000), 500, 30000, 8000)
        throttle = _number(sample.get("throttle"), 0, 1)
        pressed = _number(sample.get("clutch_press"), 0, 1)
        slip = abs(_number(sample.get("wheel_slip"), -1, 1))
        load = _number(sample.get("clutch_load"), 0, 1)
        mismatch = abs(_number(sample.get("clutch_mismatch"), -3000, 3000))
        speed = abs(_number(sample.get("speed_mps"), -150, 150))
        kick = _number(sample.get("clutch_kick"), 0, 1)
        stalled, cranking = _flag(sample, "stalled"), _flag(sample, "cranking")
        gear = _gear(sample)
        if gear is not None:
            if self._last_gear is not None and gear != self._last_gear:
                self._events.append([0.0, 0.045, 680.0, 0.45])
            self._last_gear = gear
        if stalled and self._last_stalled is False:
            self._events.append([0.0, 0.16, 145.0, 0.65])
        self._last_stalled = stalled
        if cranking and not self._last_cranking:
            self._events.append([0.0, 0.22, 95.0, 0.5])
        self._last_cranking = cranking
        self._events = self._events[-4:]
        if kick > 0.05 and self._last_kick <= 0.05:
            self._kick_age, self._kick_level = 0.0, kick
        self._last_kick = kick

        left = np.zeros(FRAMES)
        right = np.zeros(FRAMES)
        if not stalled and rpm > 100:
            # Generic RPM cue, not a reconstruction of cylinder firing or audio.
            hz = 35 + 125 * min(1.0, rpm / limit)
            amp = (0.045 + 0.065 * throttle) * (1 + min(speed, 50) / 250)
            left += amp * self._wave("engine_l", hz)
            right += amp * 0.8 * self._wave("engine_r", hz * 1.37)
        if cranking:
            left += 0.07 * self._wave("crank_l", 28)
            right += 0.05 * self._wave("crank_r", 43)
        if _flag(sample, "wheel_data_valid"):
            slip_l = _number(sample.get('slip_left'), 0, 1)
            slip_r = _number(sample.get('slip_right'), 0, 1)
            left += slip_l * .13 * self._wave('slip_l', 75 + 80 * slip_l)
            right += slip_r * .13 * self._wave('slip_r', 75 + 80 * slip_r)
            loads = [_number(sample.get(key), 0, 1) for key in ('load_fl', 'load_fr', 'load_rl', 'load_rr')]
            car = sample.get('car_type')
            sequence = sample.get('physics_sequence')
            if car != self._road_car:
                self._road_loads = None
                self._road_level = [0.0, 0.0]
            if sequence != self._road_sequence:
                if self._road_loads is not None and speed > 1:
                    rises = [max(0, value - previous - .035) * 2.5 for value, previous in zip(loads, self._road_loads)]
                    for side in range(2):
                        self._road_level[side] = max(self._road_level[side], min(1, max(rises[side], rises[side + 2])))
                self._road_loads, self._road_sequence, self._road_car = loads, sequence, car
            envelope = np.exp(-np.arange(FRAMES) / RATE / .055)
            left += .24 * self._road_level[0] * envelope * self._wave('road_l', 62)
            right += .24 * self._road_level[1] * envelope * self._wave('road_r', 62)
            self._road_level = [level * math.exp(-FRAMES / RATE / .055) for level in self._road_level]
        else:
            self._road_loads = self._road_sequence = self._road_car = None
            self._road_level = [0.0, 0.0]
            left += slip * 0.065 * self._wave("slip_l", 73 + 30 * slip)
            right += slip * 0.14 * self._wave("slip_r", 130 + 60 * slip)
        judder = load * (1 - pressed) * min(1.0, mismatch / 160)
        envelope = 0.5 + 0.5 * self._wave("judder_envelope", 12 + min(23, mismatch / 20))
        left += judder * 0.13 * envelope * self._wave("judder_l", 57)
        right += judder * 0.08 * envelope * self._wave("judder_r", 107)
        if _flag(sample, "grinding"):
            left += 0.09 * (self._wave("grind_l1", 97) + 0.5 * self._wave("grind_l2", 293))
            right += 0.10 * (self._wave("grind_r1", 149) + 0.5 * self._wave("grind_r2", 337))
        t = np.arange(FRAMES) / RATE
        age = self._kick_age + t
        impact = self._kick_level * np.exp(-age * 55) * (age < 0.12)
        left += 0.22 * impact * np.sin(2 * math.pi * 62 * age)
        right -= 0.15 * impact * np.sin(2 * math.pi * 91 * age)
        self._kick_age = min(1.0, self._kick_age + FRAMES / RATE)
        attack = np.minimum(1.0, (self._attack + t) / 0.005)
        self._attack = min(1.0, self._attack + FRAMES / RATE)

        speaker = np.zeros(FRAMES)
        remaining = []
        for event_age, duration, hz, amp in self._events:
            age = event_age + t
            window = np.sin(math.pi * np.clip(age / duration, 0, 1)) ** 2
            speaker += amp * window * np.sin(2 * math.pi * hz * age) * (age < duration)
            if event_age + FRAMES / RATE < duration:
                remaining.append([event_age + FRAMES / RATE, duration, hz, amp])
        self._events = remaining
        pcm = np.empty((FRAMES, CHANNELS))
        pcm[:, 0] = pcm[:, 1] = np.clip(speaker, -1, 1) * self.speaker_gain
        pcm[:, 2] = np.clip(left * attack, -HAPTIC_PEAK, HAPTIC_PEAK) * self.gain
        pcm[:, 3] = np.clip(right * attack, -HAPTIC_PEAK, HAPTIC_PEAK) * self.gain
        return np.rint(pcm * 32767).astype("<i2")

    def _start(self, now):
        if now < self._retry_at:
            return False
        self._retry_at = now + 1.0
        try:
            graph = subprocess.run(['pw-dump'], check=True, capture_output=True, text=True, timeout=.5)
            target = audio_target(json.loads(graph.stdout))
            self._process = subprocess.Popen([
                "pw-cat", "--playback", "--raw", "--rate", str(RATE),
                "--channels", "4", "--channel-map", "FL,FR,RL,RR",
                "--format", "s16", "--latency", "20ms", "--target", target,
                "--properties", json.dumps({
                    "node.name": "nfsu2.dualsense.effects",
                    "media.name": "NFSU2 DualSense effects", "media.role": "Game",
                    "node.dont-fallback": True, "node.dont-reconnect": True,
                    "stream.dont-remix": True}), "-"],
                stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, bufsize=0)
            fd = self._process.stdin.fileno()
            os.set_blocking(fd, False)
            if os.fpathconf(fd, "PC_PIPE_BUF") < QUANTUM_BYTES:
                raise OSError("pipe cannot preserve complete PCM blocks")
            if fcntl.fcntl(fd, fcntl.F_GETPIPE_SZ) > 8192:
                fcntl.fcntl(fd, fcntl.F_SETPIPE_SZ, 8192)
            if fcntl.fcntl(fd, fcntl.F_GETPIPE_SZ) > 8192:
                raise OSError("audio pipe exceeds bounded queue")
            self.last_error = None
            return True
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            self.last_error = str(exc)
            self.close()
            return False

    def _write(self, data):
        fd = self._process.stdin.fileno()
        for offset in range(0, len(data), QUANTUM_BYTES):
            block = data[offset:offset + QUANTUM_BYTES]
            try:
                if os.write(fd, block) != len(block):
                    raise OSError("partial PCM block")
            except BlockingIOError:
                self.dropped_blocks += 1

    def tick(self, sample, now):
        """Send one 20 ms frame. True means output attempted, not proof of sound."""
        pcm = self.render(sample, now)
        active = isinstance(sample, dict) and _flag(sample, "active")
        stamp = _number(now, -1e20, 1e20, math.nan)
        if not active or not math.isfinite(stamp):
            self.close()
            return False
        if self._process is not None and self._process.poll() is not None:
            self.last_error = "pw-cat exited"
            self.close()
        if self._process is None and not self._start(stamp):
            return False
        try:
            self._write(pcm.tobytes())
            return True
        except (OSError, ValueError) as exc:
            self.last_error = str(exc)
            self.close()
            return False

    def close(self):
        """Queue silence, drain boundedly, then stop only the child we created."""
        process, self._process = self._process, None
        self._reset()
        if process is None:
            return
        try:
            if process.poll() is None:
                fd = process.stdin.fileno()
                deadline = time.monotonic() + 0.03
                for offset in range(0, len(SILENCE), QUANTUM_BYTES):
                    while time.monotonic() < deadline:
                        try:
                            os.write(fd, SILENCE[offset:offset + QUANTUM_BYTES])
                            break
                        except BlockingIOError:
                            select.select([], [fd], [], max(0, deadline - time.monotonic()))
        except (OSError, ValueError):
            pass
        finally:
            if process.stdin is not None:
                try:
                    process.stdin.close()
                except OSError:
                    pass
        for timeout, action in ((0.08, None), (0.02, process.terminate), (0.02, process.kill)):
            try:
                if action is not None:
                    action()
                process.wait(timeout=timeout)
                break
            except subprocess.TimeoutExpired:
                continue
            except OSError:
                break
