#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'standalone/UgripperRuntime/ego'))
import ego_time_sync_service as sync
import ego_recording_worker as worker


class SyncTests(unittest.TestCase):
    def test_wire_format_and_fragment_recovery(self):
        request = bytearray(24)
        request[:3] = b'\xa0\x01\x01'
        struct.pack_into('>IQ', request, 4, 123, 998877)
        buffer = bytearray(b'garbage\xa0')
        self.assertIsNone(sync.pop_packet(buffer))
        buffer.extend(request[1:8])
        self.assertIsNone(sync.pop_packet(buffer))
        buffer.extend(request[8:] + request)
        packet = sync.pop_packet(buffer)
        self.assertEqual(packet, request)
        self.assertEqual(sync.pop_packet(buffer), request)
        with mock.patch.object(sync, 'ntp_now', return_value=9000):
            result = sync.response(packet, 8000)
        self.assertEqual(len(result), 40)
        self.assertEqual(result[:4], b'\xa0\x01\x02\x00')
        self.assertEqual(struct.unpack('>IQQQ', result[4:32]), (123, 998877, 8000, 9000))

    def test_only_bound_usb_identity(self):
        ports = [SimpleNamespace(vid=0x18d1, pid=0x903b, serial_number='other', device='/dev/ttyACM0'),
                 SimpleNamespace(vid=0x18d1, pid=0x903b, serial_number='bound', device='/dev/ttyACM1')]
        self.assertEqual(sync.bound_port('bound', ports), '/dev/ttyACM1')
        self.assertIsNone(sync.bound_port('missing', ports))
        self.assertIsNone(sync.bound_port('bound', ports + [ports[1]]))

    def test_host_boot_age_and_clock_jump(self):
        state = dict(status='ok', boot_id=sync.BOOT_ID, realtime_s=1000, monotonic_s=100)
        self.assertTrue(sync.host_ready(state, 101, 1001))
        self.assertFalse(sync.host_ready(state, 101, 1002))
        self.assertFalse(sync.host_ready(state, 99, 999))
        self.assertFalse(sync.host_ready(state, 100000, 100900))
        self.assertFalse(sync.host_ready(dict(state, boot_id='old'), 101, 1001))

    def test_applied_confirmation_and_real_sample_freshness(self):
        state = dict(status='measured', good_samples=5, result_monotonic_s=100)
        self.assertEqual(sync.combined_status(state, {'status': 'ok'}, True, 101)['status'], 'ok')
        for changed, applied, host, now in [
                (dict(state, good_samples=4), {'status': 'ok'}, True, 101),
                (state, {'status': 'error'}, True, 101),
                (state, {'status': 'ok'}, False, 101),
                (state, {'status': 'ok'}, True, 116),
                (state, {'status': 'ok'}, True, 99)]:
            self.assertNotEqual(sync.combined_status(changed, applied, host, now)['status'], 'ok')

    def test_offline_main_opens_bound_session(self):
        stop = mock.Mock()
        stop.is_set.side_effect = [False, True]
        session = mock.Mock()
        session.snapshot.return_value = dict(result_count=0, request_batches=1)
        session.confirmed_batches = 0
        session.previous_batch = 0
        with tempfile.TemporaryDirectory() as directory, \
             mock.patch.object(sync, 'RUNTIME', Path(directory)), \
             mock.patch.object(sync.threading, 'Event', return_value=stop), \
             mock.patch.object(sync.signal, 'signal'), \
             mock.patch.object(worker, 'read_status', return_value={'serial': 'bound'}), \
             mock.patch.object(sync, 'bound_port', return_value='/dev/ttyACM0'), \
             mock.patch.object(worker, 'Adb'), \
             mock.patch.object(worker, 'select_app', return_value=sync.SYNC_PACKAGE), \
             mock.patch.object(worker, 'read_serial_time_sync', return_value={'status': 'ok'}), \
             mock.patch.object(worker, 'write_json_atomic'), \
             mock.patch.object(sync, 'Session', return_value=session) as create:
            sync.main()
            create.assert_called_once_with('bound', '/dev/ttyACM0')

    def test_record_gate_never_sets_clock_and_rejects_wrong_binding(self):
        adb = SimpleNamespace(serial='bound', package='com.ssnwt.helloxr')
        now = time.monotonic()
        state = dict(status='ok', package=adb.package, serial='bound', boot_id=sync.BOOT_ID,
                     updated_monotonic_s=now, result_monotonic_s=now, offset_ms=.1)
        for serial, stamp, expected in [('bound', now, 'ok'), ('other', now, 'error'),
                                         ('bound', now - 6, 'error')]:
            with mock.patch.object(worker, 'read_status', side_effect=[
                    dict(state, updated_monotonic_s=stamp), {'serial': serial}]), \
                 mock.patch.object(worker, 'set_remote_time_ms') as setter, \
                 mock.patch.object(worker, 'read_serial_time_sync') as http:
                self.assertEqual(worker.sync_ego_time(adb)['status'], expected)
                setter.assert_not_called()
                http.assert_not_called()


if __name__ == '__main__':
    unittest.main()
