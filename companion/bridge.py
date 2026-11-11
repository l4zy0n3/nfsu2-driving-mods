#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
# DualSense protocol/calibration based on GPL-2.0-or-later hid-playstation and dualsensectl.
# Original reference copyright (c) 2020-2022 Sony Interactive Entertainment.
# See THIRD_PARTY_NOTICES.md for provenance and modification details.
"""NFSU2 DualSense USB feedback bridge. Normal controller input stays native."""
import argparse
import fcntl
import json
import mmap
import os
from pathlib import Path
import signal
import socket
import stat
import struct
import subprocess
import sys
import time

from ds_core import DEFAULTS, GYRO_MAGIC, Gestures, config_checked, feedback, gyro_calibration, output_report, telemetry, usb_input
from ds_motion import accel_calibration

ROOT = Path(__file__).resolve().parent
RUNTIME = Path(f'/tmp/nfsu2-driving-mods-{os.getuid()}')
CONFIG = Path.home() / '.config/nfsu2-driving-mods/dualsense.json'
MAILBOX = RUNTIME / 'telemetry.bin'
GYRO = RUNTIME / 'gyro.bin'


def private_runtime():
    RUNTIME.mkdir(mode=0o700, exist_ok=True)
    info = RUNTIME.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise RuntimeError('Unsafe runtime directory')


def load_config():
    return config_checked(json.loads(CONFIG.read_text())) if CONFIG.exists() else DEFAULTS.copy()


def save_config(config):
    CONFIG.parent.mkdir(parents=True, exist_ok=True)
    temporary = CONFIG.with_suffix('.tmp')
    temporary.write_text(json.dumps(config_checked(config), indent=2) + '\n')
    temporary.replace(CONFIG)


def open_regular(path, size, writable=False):
    flags = (os.O_RDWR | os.O_CREAT if writable else os.O_RDONLY) | os.O_NOFOLLOW | os.O_CLOEXEC
    fd = os.open(path, flags, 0o600)
    try:
        info = os.fstat(fd)
        if info.st_uid != os.getuid() or not stat.S_ISREG(info.st_mode):
            raise RuntimeError('Unsafe mailbox')
        if writable and info.st_size == 0:
            os.ftruncate(fd, size)
        elif info.st_size != size:
            raise RuntimeError('Wrong mailbox size')
        return mmap.mmap(fd, size, access=mmap.ACCESS_WRITE if writable else mmap.ACCESS_READ)
    finally:
        os.close(fd)


class Telemetry:
    def __init__(self):
        self.mapping = None
        self.sequence = None
        self.changed = 0
        self.ready = False

    def read(self, now):
        if self.mapping is None:
            try:
                self.mapping = open_regular(MAILBOX, 128)
            except (OSError, RuntimeError):
                return None
        sequence = struct.unpack_from('<I', self.mapping, 8)[0]
        data = self.mapping[:]
        if sequence & 1 or struct.unpack_from('<I', self.mapping, 8)[0] != sequence:
            return None
        sample = telemetry(data)
        if sample is None or sample['sequence'] != sequence:
            return None
        if self.sequence != sequence:
            self.ready = self.sequence is not None
            self.sequence, self.changed = sequence, now
        return sample if self.ready and now - self.changed <= .25 else None


class Commands:
    def __init__(self):
        self.mapping = open_regular(GYRO, 32, True)
        self.sequence = 0

    def write(self, owner=False, active=False, yaw=0, pitch=0):
        self.sequence = (self.sequence + 2) & 0xfffffffe
        struct.pack_into('<I', self.mapping, 0, self.sequence | 1)
        struct.pack_into('<IIIQff', self.mapping, 4, GYRO_MAGIC, 1,
                         int(active) | (int(owner) << 1), time.time_ns() // 1000000, yaw, pitch)
        struct.pack_into('<I', self.mapping, 0, self.sequence)


class Device:
    def __init__(self):
        self.fd = None
        self.path = None
        self.factors = None
        self.accel_factors = None
        self.last_input = None
        self.input_time = 0
        self.input_count = self.output_count = 0
        self.last_output = None
        self.output_time = 0

    def connect(self):
        for entry in Path('/sys/class/hidraw').glob('hidraw*'):
            info = (entry / 'device/uevent').read_text()
            if 'HID_ID=0003:0000054C:00000CE6' not in info:
                continue
            self.path = '/dev/' + entry.name
            self.fd = os.open(self.path, os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
            identity = bytearray(8)
            fcntl.ioctl(self.fd, 0x80084803, identity, True)
            if struct.unpack('<IHH', identity) != (3, 0x054c, 0x0ce6):
                raise OSError('HID identity changed while opening')
            report = bytearray(41)
            report[0] = 5
            try:
                fcntl.ioctl(self.fd, 0xC0294807, report, True)
                self.factors = gyro_calibration(report)
            except (OSError, ValueError) as error:
                print('Gyro disabled:', error, flush=True)
                self.factors = None
            try:
                self.accel_factors = accel_calibration(report)
            except ValueError as error:
                print('Gravity correction disabled:', error, flush=True)
                self.accel_factors = None
            # Take LED control once; repeated light-on setup overrides RGB colors.
            reset = bytearray(63)
            reset[0], reset[2] = 2, 8
            self.output(bytes(reset), time.monotonic(), force=True)
            self.last_output = None
            return True
        return False

    def read(self, now):
        reports = []
        for _ in range(256):
            try:
                packet = os.read(self.fd, 128)
            except BlockingIOError:
                break
            if not packet:
                raise OSError('Controller disconnected')
            parsed = usb_input(packet)
            if parsed:
                if self.accel_factors:
                    parsed['accel_g'] = tuple((raw - bias) * scale for raw, (bias, scale)
                                              in zip(parsed['accel'], self.accel_factors))
                self.last_input, self.input_time = parsed, now
                self.input_count += 1
                reports.append(parsed)
        return reports

    def output(self, packet, now, force=False, keepalive=True):
        if force or packet != self.last_output or (keepalive and now - self.output_time > .25):
            if os.write(self.fd, packet) != len(packet):
                raise OSError('Short HID output write')
            self.last_output, self.output_time = packet, now
            self.output_count += 1

    def close(self):
        if self.fd is not None:
            try:
                self.output(output_report(), time.monotonic(), True)
            except OSError:
                pass
            os.close(self.fd)
        self.fd = None
        self.last_input = None
        self.input_time = 0
        self.factors = self.accel_factors = None


def rpc(request):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(2)
        client.connect(str(RUNTIME / 'control.sock'))
        client.sendall(json.dumps(request).encode() + b'\n')
        data = bytearray()
        while not data.endswith(b'\n') and len(data) < 65536:
            chunk = client.recv(4096)
            if not chunk:
                break
            data.extend(chunk)
        return json.loads(data)


def demo_sample(elapsed):
    loads = dict(load_fl=.5, load_fr=.5, load_rl=.5, load_rr=.5)
    if elapsed < 6 and elapsed % 1 < .2:
        loads['load_fl' if elapsed < 3 else 'load_fr'] = .95
    return dict(rpm=0 if elapsed < 6 else 3000, rpm_limit=7500,
                clutch_press=.5, throttle=0, speed_mps=10, wheel_slip=0,
                clutch_load=.8 if 6 <= elapsed < 9 else 0,
                clutch_mismatch=100 if 6 <= elapsed < 9 else 0, clutch_kick=0,
                gear=2, stalled=False, cranking=False, grinding=False,
                active=True, manual=True, wheel_data_valid=True, clutch_trigger=1, throttle_trigger=2,
                physics_sequence=int(elapsed * 25), slip_left=0, slip_right=0, **loads)


def media_command(action):
    commands = {'playPause': 'play-pause', 'next': 'next', 'previous': 'previous'}
    return ['playerctl', commands[action]] if action in commands else None


def audio_feedback(audio, sample, config, now, headphones=False):
    audio.set_gain(config['haptic_gain'] if config['haptics'] else 0)
    audio.speaker_gain = config['speaker_gain'] if config['speaker'] and not headphones else 0
    enabled = config['enabled'] and (config['haptics'] or config['speaker'])
    # Release native rumble ownership if the optional PCM transport cannot run.
    return audio.tick(sample if enabled else None, now)


def daemon():
    from ds_audio import AudioEffects
    private_runtime()
    lock = (RUNTIME / 'lock').open('a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        return
    path = RUNTIME / 'control.sock'
    if path.exists():
        path.unlink()
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(path))
    server.listen(4)
    server.setblocking(False)
    config = load_config()
    reader, commands, device, gestures = Telemetry(), Commands(), Device(), Gestures()
    audio = AudioEffects(gain=config['haptic_gain'], speaker_gain=config['speaker_gain'])
    running, reconnect_at = True, 0.0
    focused = False
    status = {'connected': False, 'active': False}
    children = []
    demo_end = 0.0
    demo_start = 0.0

    def stop(*_):
        nonlocal running
        running = False
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        while running:
            start = now = time.monotonic()
            sample = reader.read(now)
            focused = bool(sample and sample.get('focused'))
            if now < demo_end:
                sample = demo_sample(now - demo_start)
            active = bool(sample and sample['active'] and config['enabled'])
            gyro_active = False
            yaw = pitch = 0
            if device.fd is None and now >= reconnect_at:
                reconnect_at = now + 2
                try:
                    device.connect()
                    gestures = Gestures()
                except OSError as error:
                    device.close()
                    print('Controller open:', error, flush=True)
            try:
                if device.fd is not None:
                    reports = device.read(now)
                    for value in reports:
                        actions, gyro_active, yaw, pitch = gestures.update(value, device.factors, now, config, focused)
                        for action in actions:
                            children = [p for p in children if p.poll() is None]
                            command = media_command(action)
                            if command and len(children) < 2:
                                try:
                                    children.append(subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
                                except OSError:
                                    pass  # Optional playerctl does not affect driving or feedback.
                    alive = now - device.input_time < .25
                    if not reports:
                        gyro_active = bool(alive and focused and config['gyro'] and gestures.start and gestures.start[0] >= 1440 and device.factors)
                        yaw, pitch = gestures.yaw, gestures.pitch
                    if not alive:
                        active = gyro_active = False
                        gestures.update(None, device.factors, now, config, False)
                        yaw = pitch = 0
                    output_sample = sample if active else None
                    left, right, color, leds = feedback(output_sample, config, now)
                    device.output(output_report(left, right, color, leds, active and config['speaker']), now, keepalive=active)
                    pcm_active = audio_feedback(audio, output_sample, config, now,
                                                (device.last_input or {}).get('headphones', False))
                    commands.write(owner=pcm_active, active=gyro_active and active, yaw=yaw, pitch=pitch)
                else:
                    active = False
                    commands.write()
                    audio.tick(None, now)
            except (OSError, ValueError) as error:
                print('Controller I/O:', error, flush=True)
                device.close()
                commands.write()
                audio.tick(None, now)
                active = False
            status = dict(connected=device.fd is not None, device=device.path,
                          active=active, focused=focused, gyro_calibrated=device.factors is not None,
                          accel_calibrated=device.accel_factors is not None,
                          gravity_tracking=gestures.gravity.vector is not None,
                          gyro_active=gyro_active, input_reports=device.input_count,
                          output_reports=device.output_count, config=config,
                          audio_error=audio.last_error, audio_dropped_blocks=audio.dropped_blocks,
                          telemetry=sample, input=device.last_input)
            try:
                client, _ = server.accept()
            except BlockingIOError:
                client = None
            if client:
                with client:
                    client.settimeout(.1)
                    try:
                        request = json.loads(client.recv(4096))
                        op = request.get('op')
                        if op == 'set':
                            config = config_checked(config | request['values'])
                            save_config(config)
                        elif op == 'stop':
                            running = False
                        elif op == 'demo':
                            demo_start, demo_end = now, now + 12
                        elif op != 'status':
                            raise ValueError('Unknown command')
                        response = status | {'config': config}
                    except (ValueError, KeyError, TypeError, OSError) as error:
                        response = {'error': str(error)}
                    try:
                        client.sendall(json.dumps(response).encode() + b'\n')
                    except OSError:
                        pass
            time.sleep(max(0, .02 - (time.monotonic() - start)))
    finally:
        commands.write()
        audio.close()
        device.close()
        server.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['start', 'daemon', 'status', 'stop', 'demo', 'set'])
    parser.add_argument('setting', nargs='?')
    parser.add_argument('value', nargs='?')
    args = parser.parse_args()
    private_runtime()
    if args.command == 'daemon':
        return daemon()
    if args.command == 'start':
        try:
            rpc({'op': 'status'})
            return
        except (OSError, ValueError):
            with (RUNTIME / 'bridge.log').open('a') as log:
                subprocess.Popen([sys.executable, str(Path(__file__).resolve()), 'daemon'], stdin=subprocess.DEVNULL,
                                 stdout=log, stderr=log, start_new_session=True, close_fds=True)
            for _ in range(40):
                time.sleep(.05)
                try:
                    rpc({'op': 'status'})
                    return
                except (OSError, ValueError):
                    pass
            raise RuntimeError('Bridge did not start; inspect ' + str(RUNTIME / 'bridge.log'))
    request = {'op': args.command}
    if args.command == 'set':
        request['values'] = {args.setting: json.loads(args.value)}
    print(json.dumps(rpc(request), indent=2))


if __name__ == '__main__':
    main()
