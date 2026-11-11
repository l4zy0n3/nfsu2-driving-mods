# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
from contextlib import ExitStack
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import bridge
import run


class LaunchTests(unittest.TestCase):
    def setUp(self):
        self.stack = ExitStack(); self.addCleanup(self.stack.close)
        self.directory = Path(self.stack.enter_context(tempfile.TemporaryDirectory()))
        for key, value in (('RUNTIME', self.directory), ('MAILBOX', self.directory / 'telemetry.bin'), ('GYRO', self.directory / 'gyro.bin')):
            self.stack.enter_context(patch.object(bridge, key, value))

    def test_foreground_command_gets_private_paths_and_stops_bridge(self):
        with patch.object(bridge, 'rpc', side_effect=[OSError('absent'), {}]) as rpc, patch.object(run.subprocess, 'run', return_value=subprocess.CompletedProcess([], 7)) as execute:
            self.assertEqual(run.main(['--', 'wine', 'game path/SPEED2.EXE']), 7)
            self.assertEqual(execute.call_args.args[0], ['wine', 'game path/SPEED2.EXE'])
            env = execute.call_args.kwargs['env']
            self.assertEqual(env['NFSU2_TELEMETRY_PATH'], run.wine_path(bridge.MAILBOX))
            self.assertEqual(bridge.MAILBOX.stat().st_size, 128)
            self.assertEqual(bridge.MAILBOX.stat().st_mode & 0o777, 0o600)
            rpc.assert_called_with({'op': 'stop'})

    def test_existing_bridge_prevents_another_launch(self):
        with patch.object(bridge, 'rpc', return_value={}), patch.object(run.subprocess, 'run') as execute:
            with self.assertRaises(RuntimeError): run.main(['wine', 'SPEED2.EXE'])
            execute.assert_not_called()

    def test_launch_failure_still_stops_own_bridge(self):
        with patch.object(bridge, 'rpc', side_effect=[OSError('absent'), {}]) as rpc, patch.object(run.subprocess, 'run', side_effect=[subprocess.CompletedProcess([], 0), OSError('missing game')]):
            with self.assertRaises(OSError): run.main(['missing'])
            rpc.assert_called_with({'op': 'stop'})


if __name__ == '__main__': unittest.main()
