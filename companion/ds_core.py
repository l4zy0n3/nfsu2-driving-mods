# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
# DualSense protocol/calibration based on GPL-2.0-or-later hid-playstation and dualsensectl.
# Original reference copyright (c) 2020-2022 Sony Interactive Entertainment.
# See THIRD_PARTY_NOTICES.md for provenance and modification details.
"""DualSense USB protocol and NFSU2 feedback policy; no hardware I/O.

Report layout: Sony hid-playstation and nowrep/dualsensectl.
Trigger profiles are perceptual cues, not measured pedal force curves.
"""
import math
import struct
from ds_motion import Gravity

MAGIC = 0x3544534E
GYRO_MAGIC = 0x3247594E
OFF = bytes([5]) + bytes(10)
DEFAULTS = dict(enabled=True, clutch=True, throttle=True, haptics=True,
                speaker=True, leds=True, touchpad=True, gyro=True,
                trigger_gain=0.65, haptic_gain=0.5, speaker_gain=0.08,
                gyro_gain=1.0)


def clamp(value, low=0.0, high=1.0):
    return min(high, max(low, value)) if math.isfinite(value) else low


def config_checked(values):
    result = DEFAULTS.copy()
    for key, value in values.items():
        if key not in result:
            raise ValueError('Unknown setting: ' + key)
        if isinstance(result[key], bool):
            if not isinstance(value, bool):
                raise ValueError(key + ' must be true or false')
        elif not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(value) or not 0 <= value <= (2 if key == 'gyro_gain' else .12 if key == 'speaker_gain' else 1):
            raise ValueError(key + ' is outside its range')
        result[key] = value
    return result


def telemetry(data):
    if len(data) != 128:
        return None
    magic, version, sequence, tick, flags, gear = struct.unpack_from('<5Ii', data)
    if magic != MAGIC or version != 1 or sequence & 1 or flags & ~1023 or not 0 <= gear <= 9:
        return None
    clutch_trigger, throttle_trigger = (flags >> 6) & 3, (flags >> 8) & 3
    if clutch_trigger == 3 or throttle_trigger == 3 or (clutch_trigger and clutch_trigger == throttle_trigger):
        return None
    values = struct.unpack_from('<14f', data, 24)
    if not all(math.isfinite(v) for v in values):
        return None
    keys = ('rpm', 'rpm_limit', 'clutch_press', 'throttle', 'speed_mps',
            'wheel_slip', 'clutch_load', 'clutch_mismatch', 'clutch_kick',
            'load_fl', 'load_fr', 'load_rl', 'load_rr', 'impact')
    result = dict(zip(keys, values))
    if not 0 <= result['rpm'] <= 30000 or not 0 <= result['rpm_limit'] <= 30000 or abs(result['speed_mps']) > 500:
        return None
    for key in ('clutch_press', 'throttle', 'wheel_slip', 'clutch_load', 'clutch_kick', 'impact'):
        result[key] = clamp(result[key])
    extension = struct.unpack_from('<I', data, 116)[0]
    if extension & ~3:
        return None
    if extension & 1:
        car_type, *car_values = struct.unpack_from('<I7f', data, 84)
        if car_type > 255 or not all(math.isfinite(v) for v in car_values):
            return None
        start, end, idle, inertia, capacity, shift, peak = car_values
        if not (0 <= start < end <= 1 and 0 < idle <= 30000 and 0 < inertia <= 100 and
                .5 <= capacity <= 3 and 0 <= shift <= 100000 and 0 < peak <= 100000):
            return None
        result.update(car_type=car_type, bite_start=start, bite_end=end, idle_rpm=idle,
                      engine_inertia=inertia, clutch_capacity=capacity,
                      shift_light_rpm=shift, engine_peak=peak)
    result['wheel_data_valid'] = bool(extension & 2)
    result['physics_sequence'] = struct.unpack_from('<I', data, 80)[0]
    if extension & 2:
        slips = struct.unpack_from('<2f', data, 120)
        if not all(math.isfinite(v) and 0 <= v <= 1 for v in (*values[9:13], *slips)):
            return None
        result.update(slip_left=slips[0], slip_right=slips[1])
    result.update(sequence=sequence, tick=tick, gear=gear, active=(flags & 3) == 3, focused=bool(flags & 2),
                  stalled=bool(flags & 4), cranking=bool(flags & 8),
                  grinding=bool(flags & 16), manual=bool(flags & 32),
                  clutch_trigger=clutch_trigger, throttle_trigger=throttle_trigger)
    return result


def effect(zones, frequency=0):
    if len(zones) != 10 or any(not isinstance(x, int) or not 0 <= x <= 8 for x in zones) or not 0 <= frequency <= 255:
        raise ValueError('Invalid trigger profile')
    if not any(zones):
        return OFF
    active = sum(1 << i for i, x in enumerate(zones) if x)
    packed = sum((x - 1) << (3 * i) for i, x in enumerate(zones) if x)
    return bytes([0x26 if frequency else 0x21]) + struct.pack('<HI', active, packed) + bytes([0, 0, frequency, 0])


def feedback(sample, config, now):
    if not config['enabled'] or not sample or not sample['active']:
        return OFF, OFF, (0, 0, 0), 0
    gain = config['trigger_gain']
    left = OFF
    if config['clutch'] and sample['manual']:
        # Physics uses pedal release; trigger zones run in the opposite direction.
        start, end = sample.get('bite_start', .05), sample.get('bite_end', .95)
        center = 1 - (start + end) / 2
        width = max(.14, (end - start) * .7)
        inertia = clamp(math.sqrt(sample.get('engine_inertia', .0003) / .0003), .7, 1.35)
        strength = clamp(5 * inertia * sample.get('clutch_capacity', 1.25) / 1.25, 3, 7)
        # Rise toward the bite point, then drop away faster as the pedal disengages.
        zones = [0] + [round(gain * (1 + strength * max(0, 1 - abs(i / 10 - center) /
                       (width if i / 10 <= center else max(.1, (end - start) * .25)))))
                       for i in range(1, 9)] + [0]
        release = 1 - sample['clutch_press']
        judder = clamp(sample['clutch_load']) * clamp((abs(sample['clutch_mismatch']) - 20) / 100)
        if start < release < end and judder > .08 and not sample['stalled']:
            amplitude = round(gain * (1 + 5 * judder))
            left = effect([amplitude if start < 1 - i / 10 < end and 0 < i < 9 else 0
                           for i in range(10)], round(12 + 18 * judder))
        else:
            left = effect(zones)
    right = OFF
    if config['throttle']:
        limit = sample['rpm_limit']
        limiter = limit > 0 and sample['rpm'] >= limit * .97
        if sample['throttle'] > .08 and (limiter or sample['wheel_slip'] > .12):
            amplitude = max(1, round((2 + 3 * sample['wheel_slip']) * gain)) if gain else 0
            right = effect([0, 0] + [amplitude] * 8, 28 if limiter else 12)
        else:
            right = effect([round(x * gain) for x in (0, 0, 1, 1, 1, 1, 2, 2, 2, 1)])
    color, leds = (0, 0, 0), 0
    if config['leds']:
        # The extension carries the exact current-gear HUD shift-light threshold.
        shift = sample.get('shift_light_rpm', sample['rpm_limit'])
        threshold = shift if shift > 0 else sample['rpm_limit']
        idle = sample.get('idle_rpm', 0)
        ratio = clamp((sample['rpm'] - idle) / (threshold - idle)) if threshold > idle else 0
        color = (70, 20, 0) if sample['cranking'] else (85, 0, 0) if sample['stalled'] else (0, 40, 30)
        if not sample['stalled'] and not sample['cranking']:
            color = (0, 70, 12) if ratio < .7 else (100, 45, 0) if ratio < .9 else (110, 5, 0)
            leds = (1 << min(5, int(ratio * 5.99))) - 1
            if shift > 0 and sample['rpm'] > shift:
                color = (65, 35, 140) if int(now * 8) % 2 else (10, 0, 30)
                leds = 31 if int(now * 8) % 2 else 0
    triggers = [OFF, OFF, OFF]
    if sample.get('clutch_trigger') in (1, 2):
        triggers[sample['clutch_trigger']] = left
    if sample.get('throttle_trigger') in (1, 2):
        triggers[sample['throttle_trigger']] = right
    return triggers[1], triggers[2], color, leds


def output_report(left=OFF, right=OFF, color=(0, 0, 0), leds=0, speaker=False):
    if len(left) != 11 or len(right) != 11 or len(color) != 3 or any(not 0 <= c <= 255 for c in color) or not 0 <= leds <= 31:
        raise ValueError('Invalid USB output')
    packet = bytearray(63)
    packet[0] = 2
    # Trigger-only reports leave motor ownership to native rumble or PCM.
    packet[1] = 0x0C
    packet[2] = 0x14
    # Keep the kernel's speaker/headset routing and hardware volume untouched.
    packet[11:22], packet[22:33] = right, left
    # Do not restart the lightbar's built-in animation on RGB updates.
    packet[44] = leds | 0x20
    packet[45:48] = bytes(color)
    return bytes(packet)


def usb_input(packet):
    if len(packet) != 64 or packet[0] != 1:
        return None
    contact, lo, hi, yy = packet[33:37]
    return dict(lx=packet[1], ly=packet[2], rx=packet[3], ry=packet[4],
                l2=packet[5] / 255, r2=packet[6] / 255,
                gyro=struct.unpack_from('<3h', packet, 16),
                accel=struct.unpack_from('<3h', packet, 22),
                sensor_tick=struct.unpack_from('<I', packet, 28)[0],
                touching=not bool(contact & 0x80), touch_id=contact & 127,
                x=lo | ((hi & 15) << 8), y=(hi >> 4) | (yy << 4),
                touch_click=bool(packet[10] & 2), battery=packet[53] & 15,
                headphones=bool(packet[54] & 1))


def gyro_calibration(data):
    if len(data) != 41 or data[0] != 5:
        raise ValueError('Invalid gyro calibration report')
    values = struct.unpack_from('<11h', data, 1)
    speed = values[9] + values[10]
    factors = []
    for i in range(3):
        denominator = abs(values[3 + i * 2] - values[i]) + abs(values[4 + i * 2] - values[i])
        if denominator <= 0 or speed <= 0:
            raise ValueError('Invalid gyro calibration range')
        factor = speed / denominator
        if not .001 < factor < 1:
            raise ValueError('Implausible gyro calibration')
        factors.append(factor)
    return factors


class Gestures:
    def __init__(self):
        self.start = None
        self.yaw = self.pitch = 0.0
        self.sensor_tick = None
        self.bias = [0.0] * 3
        self.last_click = False
        self.end = (0, 0)
        self.gravity = Gravity()
        self.pitch_reference = None

    def update(self, value, factors, now, config, focused):
        actions = []
        gyro_active = False
        if not focused or not config['enabled'] or value is None:
            self.start = None
            self.yaw = self.pitch = 0.0
            self.sensor_tick = None
            self.gravity = Gravity()
            self.pitch_reference = None
            return actions, False, 0.0, 0.0
        tick = value['sensor_tick']
        if not config['gyro'] or not factors:
            self.yaw = self.pitch = 0.0
            self.sensor_tick = None
            self.gravity = Gravity()
            self.pitch_reference = None
        dt = ((tick - self.sensor_tick) & 0xffffffff) / 3000000 if self.sensor_tick is not None else 0
        if dt > .04:
            self.start = None
            self.yaw = self.pitch = 0.0
            self.pitch_reference = None
            self.gravity = Gravity()
            dt = 0
        self.sensor_tick = tick
        rates = [g * f for g, f in zip(value['gyro'], factors or (0, 0, 0))]
        if not value['touching'] and max(map(abs, rates)) < 3:
            self.bias = [b * .99 + r * .01 for b, r in zip(self.bias, rates)]
        rates = [r - b for r, b in zip(rates, self.bias)]
        tilt = self.gravity.update(value.get('accel_g'), rates, dt) if config['gyro'] and factors else None
        if value['touching']:
            self.end = (value['x'], value['y'])
            if self.start is None or self.start[3] != value['touch_id']:
                self.start = (value['x'], value['y'], now, value['touch_id'])
                self.yaw = self.pitch = 0.0
                self.pitch_reference = tilt
                dt = 0
            x, y, started, _ = self.start
            if x >= 1440 and config['gyro'] and factors:
                gyro_active = True
                gain = config['gyro_gain']
                self.yaw = clamp(self.yaw - rates[1] * dt * gain / 180, -.75, .75)
                self.pitch = clamp(self.pitch + rates[0] * dt * gain / 26, -1, 1)
                if tilt is not None and gain:
                    if self.pitch_reference is None:
                        self.pitch_reference = tilt - self.pitch * 26 / gain
                    delta = (tilt - self.pitch_reference + 180) % 360 - 180
                    self.pitch = clamp(delta * gain / 26, -1, 1)
        elif self.start:
            x, y, started, _ = self.start
            dx, dy, age = self.end[0] - x, self.end[1] - y, now - started
            if config['touchpad'] and x < 1440 and 0 <= age < 1.2:
                if abs(dx) > 250 and abs(dx) > abs(dy):
                    actions.append('next' if dx > 0 else 'previous')
                elif abs(dx) < 100 and abs(dy) < 100 and age < .3:
                    actions.append('playPause')
                elif abs(dx) < 100 and abs(dy) < 100 and age >= .6:
                    actions.append('toggle')
            self.start = None
            self.yaw = self.pitch = 0.0
            self.pitch_reference = None
        self.last_click = value['touch_click']
        return actions, gyro_active, self.yaw if gyro_active else 0, self.pitch if gyro_active else 0
