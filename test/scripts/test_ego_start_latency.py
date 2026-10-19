#!/usr/bin/env python3
"""Host-only identity-cache and Runtime-owned prewarmed worker contracts."""
import argparse
import copy
import ctypes
import importlib.util
import json
import multiprocessing
import os
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    'ego_start_worker', Path(__file__).resolve().parents[2] /
    'standalone/UgripperRuntime/ego/ego_recording_worker.py')
W = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(W)


class StartCacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.args = argparse.Namespace(serial='ego-001', binding_file=self.root / 'binding.json')
        W.write_json_atomic(self.args.binding_file, {'serial': 'ego-001', 'bound_at_ms': 123})
        self.identity = dict(transport='4', boot='00112233-4455-6677-8899-aabbccddeeff',
                             apk=['package:/data/app/ego/base.apk', '123:100:200:300'],
                             process=[['45', '12345']], roots_present=['/data/dataset'])
        self.device = mock.Mock(serial='ego-001', package=W.PACKAGE)
        self.device.dataset_roots = ['/data/dataset']
        self.device.app_info = dict(package=W.PACKAGE, version_name='1.5.0', features_supported=True)
        self.device.run.return_value = subprocess.CompletedProcess([], 0, 'Broadcast completed: result=0', '')

    def prepare(self, identity=None):
        with mock.patch.object(W, 'Adb', return_value=self.device), \
             mock.patch.object(W, 'fresh_start_identity', return_value=copy.deepcopy(identity or self.identity)), \
             mock.patch.object(W, 'detect_ego', return_value=(self.device, {})) as detect:
            result = W.prepare_start_device(self.args)
        return result, detect.call_count

    def test_preparation_caches_identity_version_roots_and_codec(self):
        adb, count = self.prepare()
        self.assertEqual(count, 1)
        with mock.patch.object(W, 'set_ego_video_codec', return_value={'returncode': 0}) as configure:
            self.assertNotIn('cached', W.cached_video_codec(adb, 'h265'))
            configure.assert_called_once()
        adb, count = self.prepare()
        self.assertEqual(count, 0)
        self.assertEqual(adb.app_info['version_name'], '1.5.0')
        with mock.patch.object(W, 'set_ego_video_codec') as configure:
            self.assertTrue(W.cached_video_codec(adb, 'h265')['cached'])
            configure.assert_not_called()
        with mock.patch.object(W, 'set_ego_video_codec', return_value={'returncode': 0}) as configure:
            W.cached_video_codec(adb, 'h264')
            configure.assert_called_once()

    def test_reconnect_reboot_upgrade_reinstall_force_full_detection(self):
        for key, value in [('transport', '5'), ('boot', 'ffffffff-4455-6677-8899-aabbccddeeff'),
                           ('apk', ['package:/data/app/new/base.apk', '999:100:201:301']),
                           ('apk', ['package:/data/app/ego/base.apk', '124:100:200:301'])]:
            with self.subTest(key=key, value=value):
                self.prepare()
                changed = copy.deepcopy(self.identity)
                changed[key] = value
                self.assertEqual(self.prepare(changed)[1], 1)

    def test_process_restart_invalidates_only_codec_not_identity_cache(self):
        adb, _ = self.prepare()
        W.cached_video_codec(adb, 'h264')
        changed = copy.deepcopy(self.identity)
        changed['process'] = [['45', '99999']]  # Reused PID with new process starttime.
        adb, count = self.prepare(changed)
        self.assertEqual(count, 0)
        with mock.patch.object(W, 'set_ego_video_codec', return_value={'returncode': 0}) as configure:
            W.cached_video_codec(adb, 'h264')
            configure.assert_called_once()

    def test_no_process_never_caches_codec(self):
        identity = copy.deepcopy(self.identity)
        identity['process'] = []
        for _ in range(2):
            adb, _ = self.prepare(identity)
            with mock.patch.object(W, 'set_ego_video_codec', return_value={'returncode': 0}) as configure:
                W.cached_video_codec(adb, 'h264')
                configure.assert_called_once()

    def test_rebinding_same_serial_invalidates_cache(self):
        self.prepare()
        W.write_json_atomic(self.args.binding_file, {'serial': 'ego-001', 'bound_at_ms': 456})
        self.assertEqual(self.prepare()[1], 1)

    def test_wrong_serial_unbound_and_generation_race_reject_start(self):
        self.prepare()
        for binding in [{}, {'serial': 'other', 'bound_at_ms': 123}, {'serial': 'ego-001'}]:
            W.write_json_atomic(self.args.binding_file, binding)
            with self.assertRaisesRegex(RuntimeError, 'binding changed'):
                self.prepare()
        W.write_json_atomic(self.args.binding_file, {'serial': 'ego-001', 'bound_at_ms': 123})
        self.prepare()
        with mock.patch.object(W, 'Adb', return_value=self.device), \
             mock.patch.object(W, 'fresh_start_identity', return_value=self.identity), \
             mock.patch.object(W, 'binding_generation', side_effect=[['ego-001', 123], ['ego-001', 456]]):
            with self.assertRaisesRegex(RuntimeError, 'binding changed'):
                W.prepare_start_device(self.args)
        self.assertFalse(W.start_cache_path(self.args.binding_file).exists())

    def test_probe_failure_discards_cache_and_never_broadcasts(self):
        self.prepare()
        with mock.patch.object(W, 'Adb', return_value=self.device), \
             mock.patch.object(W, 'fresh_start_identity', side_effect=RuntimeError('offline')):
            with self.assertRaisesRegex(RuntimeError, 'offline'):
                W.prepare_start_device(self.args)
        self.assertFalse(W.start_cache_path(self.args.binding_file).exists())
        self.device.run.assert_not_called()

    def test_changed_root_requires_fresh_discovery(self):
        self.prepare()
        missing = copy.deepcopy(self.identity)
        missing['roots_present'] = []
        with mock.patch.object(W, 'Adb', return_value=self.device), \
             mock.patch.object(W, 'fresh_start_identity', side_effect=[missing, self.identity]), \
             mock.patch.object(W, 'detect_ego', return_value=(self.device, {})) as detect:
            W.prepare_start_device(self.args)
            detect.assert_called_once()

    def test_upgrade_during_full_detection_rejects_mixed_cache(self):
        changed = copy.deepcopy(self.identity)
        changed['apk'][1] = '999:100:300:400'
        with mock.patch.object(W, 'Adb', return_value=self.device), \
             mock.patch.object(W, 'fresh_start_identity', side_effect=[self.identity, changed]), \
             mock.patch.object(W, 'detect_ego', return_value=(self.device, {})):
            with self.assertRaisesRegex(RuntimeError, 'changed during detection'):
                W.prepare_start_device(self.args)
        self.assertFalse(W.start_cache_path(self.args.binding_file).exists())

    def test_optional_fingerprint_failure_uses_fully_probed_uncached_device(self):
        self.prepare()
        with mock.patch.object(W, 'Adb', return_value=self.device), \
             mock.patch.object(W, 'fresh_start_identity', side_effect=W.StartIdentityUnavailable('stat denied')), \
             mock.patch.object(W, 'detect_ego', return_value=(self.device, {})) as detect:
            adb = W.prepare_start_device(self.args)
            detect.assert_called_once_with('ego-001')
            self.assertFalse(adb.start_cache_hit)
        self.assertFalse(W.start_cache_path(self.args.binding_file).exists())

    def test_codec_failure_does_not_cache(self):
        adb, _ = self.prepare()
        with mock.patch.object(W, 'set_ego_video_codec', return_value={'returncode': 1}):
            with self.assertRaisesRegex(RuntimeError, 'codec configuration failed'):
                W.cached_video_codec(adb, 'h265')
        self.assertNotIn('codec', W.read_status(W.start_cache_path(self.args.binding_file)))

    def test_session_uses_one_root_set_without_repeated_find(self):
        adb, _ = self.prepare()
        adb.run.return_value = subprocess.CompletedProcess([], 0, 'episode_new-temp\n', '')
        with mock.patch.object(W, 'list_dataset_roots', side_effect=AssertionError('rediscovery')):
            self.assertEqual(W.find_new_temp_episodes(adb, {'/data/dataset': {'episode_old-temp'}}),
                             [('/data/dataset', 'episode_new-temp')])
        self.assertIn('/data/dataset', adb.run.call_args.args[1])

    def test_combined_properties_query(self):
        adb = mock.Mock()
        adb.run.return_value = subprocess.CompletedProcess([], 0,
            '\n'.join(f'[{key}]: [{value}]' for key, value in W.SXR_PROPS.items()) + '\n', '')
        self.assertEqual(W.get_props(adb), W.SXR_PROPS)
        adb.run.assert_called_once_with('shell', 'getprop', timeout=5)

    def test_fresh_identity_executes_real_shell_and_stats(self):
        apk = self.root / 'base.apk'
        apk.write_bytes(b'APK')
        roots = [str(self.root)]
        class ShellAdb:
            serial = 'ego-001'
            package = W.PACKAGE
            def run(inner, *args, **kwargs):
                if args[0] == 'devices':
                    return subprocess.CompletedProcess([], 0,
                        'List of devices attached\nego-001 device model:SXR_1 transport_id:7\n', '')
                self.assertEqual(args[0], 'shell')
                definitions = f"pm() {{ printf 'package:%s\\n' '{apk}'; }}; pidof() {{ echo {os.getpid()}; }}; "
                return subprocess.run(['sh', '-c', definitions + args[1]], text=True,
                                      capture_output=True, check=False)
        result = W.fresh_start_identity(ShellAdb(), roots)
        self.assertEqual(result['transport'], '7')
        self.assertEqual(result['roots_present'], roots)
        self.assertEqual(result['process'][0][0], str(os.getpid()))
        self.assertTrue(result['apk'][0].endswith('base.apk'))


class WarmWorkerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.ctx = multiprocessing.get_context('fork')

    def worker(self, request=None, before_stop=False):
        parent, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        marker = self.root / 'dispatched.json'
        episode = self.root / 'episode_owned-temp'
        def target():
            parent.close()
            args = argparse.Namespace(control_fd=child.detach(), serial='ego-001', binding_file=self.root / 'binding.json')
            def owner(args):
                marker.write_text(json.dumps(dict(serial=args.serial, episode=str(args.episode_dir), codec=args.codec)))
                return 0
            with mock.patch.object(W, 'prepare_start_device'), mock.patch.object(W, 'run_incremental_owner', side_effect=owner):
                try:
                    raise SystemExit(W.run_warm_worker(args))
                except (ValueError, TypeError):
                    raise SystemExit(2)
        process = self.ctx.Process(target=target)
        process.start()
        child.close()
        self.addCleanup(lambda: process.kill() if process.is_alive() else None)
        if request is not None:
            parent.send(json.dumps(request).encode())
        if request is None:
            parent.close()
        process.join(2)
        self.assertFalse(process.is_alive())
        if parent.fileno() >= 0:
            parent.close()
        return process.exitcode, marker, episode

    def test_single_command_transitions_same_worker_into_existing_owner(self):
        episode = self.root / 'episode_owned-temp'
        code, marker, _ = self.worker(dict(serial='ego-001', episode_dir=str(episode), codec='h265'))
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(marker.read_text()), dict(serial='ego-001', episode=str(episode), codec='h265'))

    def test_runtime_closing_control_does_not_create_episode(self):
        code, marker, episode = self.worker()
        self.assertEqual(code, 0)
        self.assertFalse(marker.exists())
        self.assertFalse(episode.exists())

    def test_late_wrong_device_or_malformed_commands_never_dispatch(self):
        for request in [dict(serial='other', episode_dir=str(self.root / 'episode_owned-temp'), codec='h264'),
                        dict(serial='ego-001', episode_dir='relative', codec='h264'),
                        dict(serial='ego-001', episode_dir=str(self.root / 'episode_owned-temp'), codec='h266'),
                        {}, []]:
            with self.subTest(request=request):
                code, marker, episode = self.worker(request)
                self.assertEqual(code, 2)
                self.assertFalse(marker.exists())
                self.assertFalse(episode.exists())

    def test_repeated_command_is_rejected_after_session_dispatch(self):
        parent, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        active, release = self.ctx.Event(), self.ctx.Event()
        def target():
            parent.close()
            args = argparse.Namespace(control_fd=child.detach(), serial='ego-001', binding_file=self.root / 'binding.json')
            def owner(unused):
                active.set()
                assert release.wait(2)
                return 0
            with mock.patch.object(W, 'prepare_start_device'), mock.patch.object(W, 'run_incremental_owner', side_effect=owner):
                raise SystemExit(W.run_warm_worker(args))
        process = self.ctx.Process(target=target)
        process.start()
        child.close()
        try:
            packet = json.dumps(dict(serial='ego-001', episode_dir=str(self.root / 'episode-temp'), codec='h264')).encode()
            parent.send(packet)
            self.assertTrue(active.wait(2))
            with self.assertRaises((BrokenPipeError, ConnectionResetError)):
                parent.send(packet)
        finally:
            release.set()
            parent.close()
            process.join(2)
            if process.is_alive():
                process.kill()
                process.join(1)
        self.assertEqual(process.exitcode, 0)

    def test_duplicate_start_waits_for_owner_and_obeys_stop_tombstone(self):
        args = argparse.Namespace(episode_dir=self.root / 'episode-temp', status_file=self.root / 'episode-temp/ego/ego_sync.json')
        active, duplicate_started, release = self.ctx.Event(), self.ctx.Event(), self.ctx.Event()
        def first():
            def start(unused):
                active.set()
                assert release.wait(2)
                return 0
            with mock.patch.object(W, '_run_start', side_effect=start):
                raise SystemExit(W.run_incremental_owner(args))
        def duplicate():
            def start(unused):
                duplicate_started.set()
                return 0
            with mock.patch.object(W, '_run_start', side_effect=start):
                raise SystemExit(W.run_incremental_owner(args))
        owner = self.ctx.Process(target=first)
        replacement = self.ctx.Process(target=duplicate)
        owner.start()
        try:
            self.assertTrue(active.wait(2))
            replacement.start()
            self.assertFalse(duplicate_started.wait(.05))
            W.write_json_atomic(W.stop_request_path(args.status_file), {'deadline_monotonic_s': time.monotonic() + 2})
            release.set()
            owner.join(2)
            replacement.join(2)
            self.assertEqual(owner.exitcode, 0)
            self.assertEqual(replacement.exitcode, 0)
            self.assertFalse(duplicate_started.is_set())
        finally:
            release.set()
            for process in (owner, replacement):
                if process.is_alive():
                    process.kill()
                    process.join(1)

    def test_runtime_crash_terminates_idle_worker_via_parent_death_signal(self):
        # Isolate subreaping in a helper process so this test owns/reaps the
        # orphaned worker and never changes the test runner's process policy.
        def probe():
            libc = ctypes.CDLL(None, use_errno=True)
            assert libc.prctl(36, 1, 0, 0, 0) == 0  # PR_SET_CHILD_SUBREAPER
            parent_socket, child_socket = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            ready = self.ctx.Event()
            receive_pid, send_pid = self.ctx.Pipe(duplex=False)
            def runtime():
                def worker():
                    parent_socket.close()
                    args = argparse.Namespace(control_fd=child_socket.detach(), serial='ego-001', binding_file=self.root / 'binding.json')
                    with mock.patch.object(W, 'prepare_start_device', side_effect=lambda args: ready.set()):
                        raise SystemExit(W.run_warm_worker(args))
                child = self.ctx.Process(target=worker)
                child.start()
                send_pid.send(child.pid)
                child_socket.close()
                while True:
                    time.sleep(.1)
            owner = self.ctx.Process(target=runtime)
            owner.start()
            child_socket.close()
            assert receive_pid.poll(2)
            worker_pid = receive_pid.recv()
            assert ready.wait(2)
            owner.kill()
            owner.join(2)
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                reaped, status = os.waitpid(worker_pid, os.WNOHANG)
                if reaped:
                    assert os.WIFSIGNALED(status) and os.WTERMSIG(status) == signal.SIGTERM
                    parent_socket.close()
                    return
                time.sleep(.01)
            os.kill(worker_pid, signal.SIGKILL)
            os.waitpid(worker_pid, 0)
            raise AssertionError('orphan warm worker survived Runtime crash')
        process = self.ctx.Process(target=probe)
        process.start()
        process.join(6)
        if process.is_alive():
            process.kill()
            process.join(1)
            self.fail('parent-death probe did not finish')
        self.assertEqual(process.exitcode, 0)

    def test_stop_tombstone_before_delayed_start_prevents_broadcast(self):
        args = argparse.Namespace(episode_dir=self.root / 'episode-temp', status_file=self.root / 'episode-temp/ego/ego_sync.json')
        W.write_json_atomic(W.stop_request_path(args.status_file), {'deadline_monotonic_s': time.monotonic() + 3})
        with mock.patch.object(W, '_run_start') as start:
            self.assertEqual(W.run_incremental_owner(args), 0)
            start.assert_not_called()


if __name__ == '__main__':
    unittest.main()
