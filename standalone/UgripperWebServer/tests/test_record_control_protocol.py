import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import recording_request as control


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(); self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name); self.lock = self.root/'lock'
        self.now = 0.
        self.cancelled = threading.Event()
        for name in ('REQUEST', 'RESULT'):
            p = patch.object(control, name, self.root/name); p.start(); self.addCleanup(p.stop)
        for target, value in (('time.monotonic', lambda: self.now), ('time.time', lambda: 100+self.now)):
            p = patch('recording_request.'+target, side_effect=value)
            p.start(); self.addCleanup(p.stop)
        self.sent = []
        self.request = control.RecordingRequest('a'*32, 'trajectory_test', self.send, self.lock, self.cancelled)

    def send(self, text):
        self.sent.append(json.loads(text))

    def reply(self, status, **extra):
        control.atomic_json(control.RESULT, dict(id=self.request.id, pid=os.getpid(), status=status, **extra))

    def tick(self, seconds):
        self.now += seconds

    def test_rejection_returns_reason_immediately(self):
        self.reply('rejected', code='ego_sync_pending', message='waiting for sync')
        with self.assertRaises(control.ControlError) as caught:
            self.request.start()
        self.assertEqual(caught.exception.code, 'ego_sync_pending')
        self.assertEqual(self.now, 0)

    def test_acceptance_timeout_cancels_late_command(self):
        with patch.object(self.cancelled, 'wait', side_effect=self.tick):
            with self.assertRaises(control.ControlError) as caught:
                self.request.start()
        self.assertEqual(caught.exception.code, 'command_not_accepted')
        self.assertLess(self.now, 5.2)
        self.assertTrue(control.read_json(control.REQUEST)['cancelled'])

    def test_starting_has_separate_budget_and_requires_recording(self):
        self.reply('starting')
        def tick(seconds):
            self.tick(seconds)
            if self.now > 6:
                self.reply('recording')
                control.atomic_json(self.lock, dict(test_request_id=self.request.id,
                    state='recording', episode_dir='/episode'))
        with patch.object(self.cancelled, 'wait', side_effect=tick):
            self.assertEqual(self.request.start(), Path('/episode'))
        self.assertGreater(self.now, 5)

    def test_cancel_while_starting_is_observed(self):
        self.reply('starting')
        def cancel(seconds): self.cancelled.set()
        with patch.object(self.cancelled, 'wait', side_effect=cancel):
            with self.assertRaises(control.ControlError) as caught:
                self.request.start()
        self.assertEqual(caught.exception.code, 'cancelled')
        self.assertTrue(control.read_json(control.REQUEST)['cancelled'])

    def test_short_lived_failure_does_not_need_lock(self):
        self.reply('failed', code='recording_start_failed', message='camera unavailable')
        with self.assertRaisesRegex(control.ControlError, 'camera unavailable'):
            self.request.start()

    def test_cleanup_reuses_deadline_and_sends_stop_once(self):
        self.request.sent = True
        self.reply('recording')
        control.atomic_json(self.lock, dict(test_request_id=self.request.id, state='recording'))
        with patch.object(control.time, 'sleep', side_effect=self.tick):
            for _ in range(2):
                with self.assertRaisesRegex(control.ControlError, 'finalization_timeout'):
                    self.request.stop()
        self.assertLess(self.now, 90.01)
        self.assertEqual(len(self.sent), 1)

    def test_cleanup_never_waits_for_other_task(self):
        self.request.sent = True
        self.reply('recording')
        control.atomic_json(self.lock, dict(test_request_id='other', state='recording'))
        self.request.stop()
        self.assertEqual(self.sent, [])

    def test_runtime_exit_while_starting_is_reported(self):
        self.reply('starting')
        with patch.object(control.os, 'kill', side_effect=ProcessLookupError):
            with self.assertRaisesRegex(control.ControlError, 'runtime_stopped'):
                self.request.start()

if __name__ == '__main__': unittest.main()
