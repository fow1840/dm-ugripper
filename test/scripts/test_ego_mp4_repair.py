#!/usr/bin/env python3
"""Bounded MP4 repair, durability and stop failure propagation."""
import argparse
from contextlib import ExitStack
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('ego_mp4_worker', ROOT / 'standalone/UgripperRuntime/ego/ego_recording_worker.py')
WORKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(WORKER)


def box(kind, payload=b''):
    return struct.pack('>I4s', len(payload) + 8, kind) + payload


def movie():
    return box(b'moov', box(b'mvhd', bytes(100)) + box(b'trak', box(b'tkhd', bytes(84)) + box(b'mdia')))


class Mp4RepairTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'video.mp4'
        self.ftyp = box(b'ftyp', b'isom0000')

    def legacy(self, old_size=0, payload=b'video payload'):
        self.path.write_bytes(self.ftyp + struct.pack('>I4sQ', 1, b'mdat', old_size) + payload + movie())
        return len(self.ftyp), 16 + len(payload)

    def test_gigabyte_regular_mdat_reads_only_headers(self):
        with self.path.open('wb') as out:
            out.write(self.ftyp + struct.pack('>I4s', 1024 ** 3, b'mdat'))
            out.truncate(len(self.ftyp) + 1024 ** 3)
        with mock.patch.object(Path, 'read_bytes', side_effect=AssertionError('whole-file read forbidden')), \
             mock.patch.object(WORKER.os, 'fsync') as sync:
            result = WORKER.repair_mp4_mdat_largesize(self.path)
        self.assertEqual(result['status'], 'skipped')
        self.assertLess(result['bytes_read'], 64)
        sync.assert_not_called()

    def test_correct_extended_mdat_does_not_write(self):
        self.legacy(old_size=16 + len(b'video payload'))
        before = self.path.read_bytes()
        with mock.patch.object(WORKER.os, 'fsync') as sync:
            result = WORKER.repair_mp4_mdat_largesize(self.path)
        self.assertEqual(result['status'], 'unchanged')
        self.assertEqual(self.path.read_bytes(), before)
        sync.assert_not_called()

    def test_legacy_repair_changes_only_eight_bytes_and_syncs(self):
        offset, expected = self.legacy()
        before = self.path.read_bytes()
        with mock.patch.object(WORKER.os, 'fsync') as sync:
            result = WORKER.repair_mp4_mdat_largesize(self.path)
        after = self.path.read_bytes()
        self.assertEqual(result['status'], 'ok', result)
        self.assertEqual(after[:offset + 8], before[:offset + 8])
        self.assertEqual(after[offset + 8:offset + 16], struct.pack('>Q', expected))
        self.assertEqual(after[offset + 16:], before[offset + 16:])
        sync.assert_called_once()

    def test_fake_moov_string_in_payload_is_not_a_movie(self):
        _, expected = self.legacy(payload=box(b'moov', b'fake') + b'payload')
        result = WORKER.repair_mp4_mdat_largesize(self.path)
        self.assertEqual(result['status'], 'ok', result)
        self.assertEqual(result['new_mdat_largesize'], expected)

    def test_invalid_movie_structure_preserves_file(self):
        self.path.write_bytes(self.ftyp + struct.pack('>I4sQ', 1, b'mdat', 0) + b'payload' + box(b'moov', b'fake'))
        before = self.path.read_bytes()
        result = WORKER.repair_mp4_mdat_largesize(self.path)
        self.assertEqual(result['status'], 'error')
        self.assertEqual(self.path.read_bytes(), before)

    def test_ambiguous_movies_preserve_file(self):
        # Both candidates can end in a free box; do not guess between them.
        inner = movie()
        trailing = box(b'free', inner)
        self.legacy(payload=b'payload')
        with self.path.open('ab') as out:
            out.write(trailing)
        before = self.path.read_bytes()
        result = WORKER.repair_mp4_mdat_largesize(self.path)
        self.assertEqual(result['status'], 'error')
        self.assertIn('ambiguous', result['error'])
        self.assertEqual(self.path.read_bytes(), before)

    def test_moov_marker_across_block_boundary(self):
        with mock.patch.object(WORKER, 'MP4_REPAIR_BLOCK_BYTES', 256):
            self.legacy(payload=bytes(256 - 6))
            result = WORKER.repair_mp4_mdat_largesize(self.path)
        self.assertEqual(result['status'], 'ok', result)

    def test_gigabyte_legacy_file_repairs_with_bounded_reads(self):
        moov_offset = 1024 ** 3
        with self.path.open('wb') as out:
            out.write(self.ftyp + struct.pack('>I4sQ', 1, b'mdat', 0))
            out.seek(moov_offset)
            out.write(movie())
        with mock.patch.object(WORKER, 'MP4_REPAIR_MAX_READ_BYTES', 128 * 1024):
            result = WORKER.repair_mp4_mdat_largesize(self.path)
        self.assertEqual(result['status'], 'ok', result)
        self.assertEqual(result['new_mdat_largesize'], moov_offset - len(self.ftyp))
        self.assertLessEqual(result['bytes_read'], 128 * 1024)

    def test_missing_movie_obeys_read_budget(self):
        with self.path.open('wb') as out:
            out.write(self.ftyp + struct.pack('>I4sQ', 1, b'mdat', 0))
            out.truncate(1024 ** 3)
        with mock.patch.object(WORKER, 'MP4_REPAIR_MAX_READ_BYTES', 128 * 1024):
            result = WORKER.repair_mp4_mdat_largesize(self.path)
        self.assertEqual(result['status'], 'error')
        self.assertLessEqual(result['bytes_read'], 128 * 1024)

    def test_box_count_limit(self):
        self.path.write_bytes(box(b'free') * 12 + box(b'mdat'))
        with mock.patch.object(WORKER, 'MP4_REPAIR_MAX_BOXES', 5):
            result = WORKER.repair_mp4_mdat_largesize(self.path)
        self.assertEqual(result['status'], 'error')
        self.assertIn('box limit', result['error'])

    def test_truncated_or_out_of_range_headers(self):
        for data in [b'', b'bad', struct.pack('>I4s', 1, b'mdat'), struct.pack('>I4s', 999, b'free')]:
            with self.subTest(data=data):
                self.path.write_bytes(data)
                result = WORKER.repair_mp4_mdat_largesize(self.path)
                self.assertEqual(result['status'], 'error')
                self.assertEqual(self.path.read_bytes(), data)

    def test_fsync_failure_is_error(self):
        self.legacy()
        with mock.patch.object(WORKER.os, 'fsync', side_effect=OSError('disk flush failed')):
            result = WORKER.repair_mp4_mdat_largesize(self.path)
        self.assertEqual(result['status'], 'error')
        self.assertIn('disk flush failed', result['error'])

    def test_fragmented_moov_first_does_not_scan_media(self):
        self.path.write_bytes(self.ftyp + movie() + box(b'moof') + box(b'mdat', bytes(10000)))
        result = WORKER.repair_mp4_mdat_largesize(self.path)
        self.assertEqual(result['status'], 'skipped')
        self.assertLess(result['bytes_read'], 100)


class StopFinalizeTests(unittest.TestCase):
    def test_repair_or_header_failure_blocks_finalize_and_cleanup(self):
        for failed_stage in ('repair_mp4_files', 'refresh_final_mp4_headers'):
            with self.subTest(stage=failed_stage), tempfile.TemporaryDirectory() as temp:
                episode = Path(temp) / 'episode_0001-temp'
                status_file = episode / 'ego/ego_sync.json'
                WORKER.write_status(status_file, {'status': 'recording', 'serial': 'test',
                    'remote_root': '/remote', 'remote_episode': 'episode_0001-temp'})
                args = argparse.Namespace(episode_dir=episode, status_file=status_file, serial='test',
                                          finalize_timeout=1, interval=0.01)
                adb = mock.Mock()
                adb.run.return_value = subprocess.CompletedProcess([], 0, 'ok', '')
                with ExitStack() as stack:
                    stack.enter_context(mock.patch.object(WORKER, 'Adb', return_value=adb))
                    stack.enter_context(mock.patch.object(WORKER, 'adb_device_ready', return_value=(True, '')))
                    stack.enter_context(mock.patch.object(WORKER, 'remote_dir_exists',
                        side_effect=lambda adb, root, name: name == 'episode_0001'))
                    stack.enter_context(mock.patch.object(WORKER, 'remote_file_sizes', return_value={}))
                    stack.enter_context(mock.patch.object(WORKER, 'sync_episode_once', return_value={}))
                    stack.enter_context(mock.patch.object(WORKER, 'recover_finalized_episode_files', return_value=(True, {}, '', '')))
                    for name in ('repair_mp4_files', 'refresh_final_mp4_headers'):
                        result = {'rgb.mp4': {'status': 'error', 'error': 'failed'}} if name == failed_stage else {}
                        stack.enter_context(mock.patch.object(WORKER, name, return_value=result))
                    stack.enter_context(mock.patch.object(WORKER, 'ugripper_log'))
                    self.assertEqual(WORKER.run_stop(args), 1)
                saved = json.loads(status_file.read_text())
                self.assertEqual(saved['status'], 'error')
                self.assertEqual(saved['error_type'], 'ego_pull_incomplete')
                self.assertIn('mp4_repair', saved['stop_timings_ms'])
                with mock.patch.object(WORKER, 'Adb') as connect, mock.patch.object(WORKER, 'remove_remote_episode') as delete:
                    WORKER.run_cleanup(args)
                    connect.assert_not_called()
                    delete.assert_not_called()

    def test_legacy_inconsistent_finalize_status_cannot_delete_remote(self):
        with tempfile.TemporaryDirectory() as temp:
            episode = Path(temp)
            status_file = episode / 'ego/ego_sync.json'
            WORKER.write_status(status_file, {'status': 'finalized', 'finalize': {'status': 'error', 'error': 'broken'}})
            args = argparse.Namespace(episode_dir=episode, status_file=status_file, serial='test')
            with mock.patch.object(WORKER, 'Adb') as connect:
                self.assertEqual(WORKER.run_cleanup(args), 1)
                connect.assert_not_called()
            self.assertEqual(json.loads(status_file.read_text())['status'], 'error')


if __name__ == '__main__':
    unittest.main()
