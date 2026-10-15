#!/usr/bin/env python3
"""Real subprocess reads exercise cancellation, exit status and bounded commits."""
import argparse
import importlib.util
import json
import os
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

SOURCE = Path(__file__).resolve().parents[2] / 'standalone/UgripperRuntime/ego/ego_recording_worker.py'
spec = importlib.util.spec_from_file_location('ego_transfer_worker', SOURCE)
W = importlib.util.module_from_spec(spec)
spec.loader.exec_module(W)


class TransferRecoveryTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.progress = mock.patch.object(W, 'incremental_progress_path', return_value=self.root / 'progress.json')
        self.progress.start()
        self.old_status = W._INCREMENTAL_STATUS_FILE
        self.old_stop = W._INCREMENTAL_STOP_REQUESTED
        W._INCREMENTAL_STATUS_FILE = None
        W._INCREMENTAL_STOP_REQUESTED = False

    def tearDown(self):
        W._INCREMENTAL_STATUS_FILE = self.old_status
        W._INCREMENTAL_STOP_REQUESTED = self.old_stop
        self.progress.stop()
        self.tmp.cleanup()

    def reader(self, code):
        adb = W.Adb.__new__(W.Adb)
        adb.command = lambda *args: [sys.executable, '-u', '-c', code]
        return adb

    def test_no_pty_and_exact_binary_content(self):
        adb = W.Adb.__new__(W.Adb)
        adb.bin, adb.serial = 'adb', 'ego'
        command = adb.command('shell', '-T', '-n', 'dd')
        self.assertEqual(command[:6], ['adb', '-s', 'ego', 'shell', '-T', '-n'])
        payload = bytes(range(256)) * 1024
        got = self.reader('import sys;sys.stdout.buffer.write(bytes(range(256))*1024)').exec_out_bytes('file', 0, len(payload))
        self.assertEqual(got, payload)

    def test_short_output_and_remote_failure_do_not_succeed(self):
        for code in ['print("x",end="")', 'import sys;sys.stdout.write("abcd");sys.exit(2)']:
            with self.assertRaises(W.TransferStalled):
                self.reader(code).exec_out_bytes('file', 0, 4)

    def test_no_progress_exits_and_reaps_adb_child(self):
        pid_path = self.root / 'adb-pid'
        adb = self.reader(f'import os,time;open({str(pid_path)!r},"w").write(str(os.getpid()));time.sleep(20)')
        before = time.monotonic()
        with mock.patch.object(W, 'TRANSFER_IDLE_TIMEOUT_SEC', .15), self.assertRaises(W.TransferStalled):
            adb.exec_out_bytes('file', 0, 4)
        self.assertLess(time.monotonic() - before, 1)
        with self.assertRaises(ProcessLookupError):
            os.kill(int(pid_path.read_text()), 0)

    def test_stop_cancels_read_and_kills_adb_that_ignores_sigterm(self):
        W._INCREMENTAL_STATUS_FILE = self.root / 'status.json'
        pid_path = self.root / 'adb-pid'
        adb = self.reader(f'import signal,os,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);open({str(pid_path)!r},"w").write(str(os.getpid()));print("a",flush=True);time.sleep(20)')
        def stop():
            W.write_json_atomic(W.stop_request_path(W._INCREMENTAL_STATUS_FILE), {'deadline_monotonic_s': time.monotonic()+3})
        timer = threading.Timer(.15, stop)
        old_deadline = W._OPERATION_DEADLINE
        before = time.monotonic()
        timer.start()
        try:
            with self.assertRaises(W.IncrementalCancelled):
                adb.exec_out_bytes('file', 0, 4)
            self.assertLess(time.monotonic()-before, 1)
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pid_path.read_text()), 0)
        finally:
            timer.join()
            W._OPERATION_DEADLINE = old_deadline

    def test_chunk_commit_resume_and_stop_boundary(self):
        local = self.root / 'data'
        local.mkdir()
        (local/'rgb.mp4').write_bytes(b'abcd')
        adb = mock.Mock()
        adb.exec_out_bytes.side_effect = [b'efgh', W.TransferStalled('read stopped'), b'ijkl', b'mn']
        with mock.patch.object(W, 'TRANSFER_CHUNK_BYTES', 4), mock.patch.object(W, 'remote_file_size', return_value=14):
            with self.assertRaises(W.TransferStalled):
                W.sync_one_file(adb, '/remote', 'episode', local, 'rgb.mp4')
            self.assertEqual((local/'rgb.mp4').read_bytes(), b'abcdefgh')
            self.assertEqual(W.sync_one_file(adb, '/remote', 'episode', local, 'rgb.mp4'), 6)
        self.assertEqual((local/'rgb.mp4').read_bytes(), b'abcdefghijklmn')
        self.assertEqual([c.args[1:] for c in adb.exec_out_bytes.call_args_list], [(4,4),(8,4),(8,4),(12,2)])

    def test_resume_never_sends_app_start_or_old_session_stop(self):
        args = argparse.Namespace(status_file=self.root/'status.json', episode_dir=self.root,
                                  serial='ego', interval=.01, recovery_count=2)
        status = dict(serial='ego', remote_root='/remote', remote_episode='episode-temp', status='recording')
        W.write_status(args.status_file, status)
        def copy(*unused):
            W.write_json_atomic(W.stop_request_path(args.status_file), {'stop': True})
            return {}
        with mock.patch.object(W, 'Adb') as factory, mock.patch.object(W, 'sync_episode_once', side_effect=copy), \
             mock.patch.object(W, 'continuous_time_sync_status', return_value={}), \
             mock.patch.object(W, 'recover_previous_recording') as recover:
            self.assertEqual(W.run_incremental_owner(args, resume=True), 0)
            factory.return_value.run.assert_not_called()
            recover.assert_not_called()
        self.assertEqual(W.read_status(args.status_file)['incremental_restarts'], 2)

    def test_incremental_stall_releases_lock_and_returns_retry_code(self):
        args = argparse.Namespace(status_file=self.root/'status.json')
        W.write_status(args.status_file, dict(serial='ego', remote_episode='episode-temp', status='recording'))
        with mock.patch.object(W, '_run_start', side_effect=W.TransferStalled('no progress')):
            self.assertEqual(W.run_start(args), 75)
        with W.episode_sync_owner(args.status_file, wait=False):
            self.assertEqual(W.read_status(args.status_file)['phase'], 'incremental_retry')


if __name__ == '__main__':
    unittest.main()
