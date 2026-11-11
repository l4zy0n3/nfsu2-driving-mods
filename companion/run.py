#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
"""Run a foreground Wine/Proton command with the optional DualSense bridge."""
import fcntl
import os
import subprocess
import sys
from pathlib import Path
import bridge


def wine_path(path):
    value = str(Path(path).resolve())
    if not value.startswith('/'):
        raise ValueError('Expected absolute Linux path')
    return 'Z:' + value.replace('/', '\\')


def main(command):
    if command[:1] == ['--']:
        command = command[1:]
    if not command:
        raise SystemExit('Usage: python3 companion/run.py -- wine /path/to/SPEED2.EXE')
    bridge.private_runtime()
    with (bridge.RUNTIME / 'launch.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        # Precreate both files with owner-only permissions before Wine opens them.
        for path, size in ((bridge.MAILBOX, 128), (bridge.GYRO, 32)):
            mapping = bridge.open_regular(path, size, True)
            mapping.close()
        try:
            bridge.rpc({'op': 'status'})
        except (OSError, ValueError):
            pass
        else:
            raise RuntimeError('Bridge already running; stop it before launching another session')
        script = str(Path(__file__).with_name('bridge.py'))
        subprocess.run([sys.executable, script, 'start'], check=True)
        env = os.environ | {'NFSU2_TELEMETRY_PATH': wine_path(bridge.MAILBOX),
                           'NFSU2_GYRO_PATH': wine_path(bridge.GYRO)}
        try:
            return subprocess.run(command, env=env).returncode
        finally:
            try:
                bridge.rpc({'op': 'stop'})
            except (OSError, ValueError):
                pass


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
