#!/usr/bin/env python3
"""Syslog finite snapshots, incremental durability, retention and rotation."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts/logging'))
import system_log_snapshot as logs
import rotate_syslog


class SystemLogSnapshotTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.disk = self.base/'disk'; self.disk.mkdir()
        self.source = self.base/'ram'; self.source.mkdir()
        self.active = self.source/'syslog.log'; self.active.write_bytes(b'first\npartial')
        self.flushes = []

    def snapshot(self, **kwargs):
        return logs.Snapshot(self.disk, self.source, 'a'*32, 'boot-a',
                             require_mount=False, flush=self.flushes.append, **kwargs)

    def manifest(self):
        return json.loads((self.disk/'system_journal/manifest.json').read_text())

    def contents(self, manifest=None):
        manifest = manifest or self.manifest()
        return b''.join((self.disk/'system_journal'/name).read_bytes()
                        for name in logs.committed_files(manifest))

    def test_incremental_complete_lines_no_rewrite(self):
        first = self.snapshot().run()
        names = logs.committed_files(first)
        inode = (self.disk/'system_journal'/names[0]).stat().st_ino
        self.assertEqual(self.contents(), b'first\n')
        self.snapshot().run()
        self.assertEqual(self.manifest()['copied_bytes'], 0)
        self.assertEqual(logs.committed_files(self.manifest()), names)
        with self.active.open('ab') as out: out.write(b'-done\nlast\n')
        self.snapshot().run()
        self.assertEqual(self.contents(), b'first\npartial-done\nlast\n')
        self.assertEqual((self.disk/'system_journal'/names[0]).stat().st_ino, inode)
        self.assertEqual(self.manifest()['format'], 'syslog-only')
        self.assertEqual(self.flushes[-1], self.disk/'system_journal')

    def test_snapshot_pins_finite_length_during_growth(self):
        snap = self.snapshot()
        original = snap.sources
        def growing(stack):
            result = original(stack)
            with self.active.open('ab') as out: out.write(b'-later\n')
            return result
        snap.sources = growing
        snap.run()
        self.assertEqual(self.contents(), b'first\n')
        self.snapshot().run()
        self.assertEqual(self.contents(), b'first\npartial-later\n')

    def test_rename_rotation_between_snapshots(self):
        self.snapshot().run()
        with self.active.open('ab') as out: out.write(b'-end\n')
        rotate_syslog.rotate(self.source)
        self.active.write_bytes(b'new\n')
        self.snapshot().run()
        self.assertEqual(self.contents(), b'first\npartial-end\nnew\n')
        self.assertEqual(len(self.manifest()['offsets']), 2)

    def test_small_rotations_have_a_file_count_limit(self):
        for i in range(20):
            self.active.write_bytes(b'x\n')
            rotate_syslog.rotate(self.source)
        self.assertEqual(len(list(self.source.glob('syslog.*.log'))), 9)
        self.assertEqual(int((self.source/'dropped_bytes').read_text()), 22)

    def test_rotation_order_survives_clock_rollback(self):
        self.active.write_bytes(b'old\n')
        with patch.object(rotate_syslog.time, 'time_ns', return_value=200):
            rotate_syslog.rotate(self.source)
        self.active.write_bytes(b'new\n')
        with patch.object(rotate_syslog.time, 'time_ns', return_value=100):
            rotate_syslog.rotate(self.source)
        self.active.write_bytes(b'last\n')
        self.snapshot().run()
        self.assertEqual(self.contents(), b'old\nnew\nlast\n')

    def test_rotation_after_pinning_does_not_lose_or_follow(self):
        snap = self.snapshot(); original = snap.sources
        def rotating(stack):
            result = original(stack)
            rotate_syslog.rotate(self.source)
            self.active.write_bytes(b'next\n')
            return result
        snap.sources = rotating
        snap.run()
        self.assertEqual(self.contents(), b'first\n')
        self.snapshot().run()
        self.assertEqual(self.contents(), b'first\nnext\n')

    def test_truncation_and_inode_reuse_guard(self):
        self.active.write_bytes(b'old-long-line\n'); self.snapshot().run()
        self.active.write_bytes(b'new-long-line\n')
        self.snapshot().run()
        self.assertEqual(self.contents(), b'old-long-line\nnew-long-line\n')
        self.assertEqual(self.manifest()['source_truncations'], 1)
        self.active.write_bytes(b'x\n'); self.snapshot().run()
        self.assertEqual(self.manifest()['source_truncations'], 2)

    def test_boot_change_preserves_persistent_source_offsets(self):
        self.snapshot().run(); snap = self.snapshot(); snap.boot = 'boot-b'; snap.run()
        self.assertEqual(self.contents(), b'first\n')
        self.assertEqual(self.manifest()['copied_bytes'], 0)
        self.assertTrue(all(k.startswith('a'*32 + ':') for k in self.manifest()['offsets']))

    def test_volume_and_total_capacity_and_age(self):
        self.active.write_bytes(b'aaaa\nbbbb\ncccc\ndddd\n')
        snap = self.snapshot(volume_limit=10, total_limit=10)
        m = snap.run()
        self.assertEqual(self.contents(), b'cccc\ndddd\n')
        self.assertEqual(m['retention_discarded_bytes'], 10)
        self.assertTrue(all(v['bytes']<=10 for v in m['volumes']))
        with patch.object(logs.time, 'time', return_value=time.time()+100):
            snap.retention=10; snap.run()
        self.assertEqual(self.contents(), b'')

    def test_new_data_never_appended_to_expired_volume(self):
        self.active.write_bytes(b'old\n');self.snapshot(retention=10).run()
        with self.active.open('ab') as out:out.write(b'new\n')
        with patch.object(logs.time,'time',return_value=time.time()+100):
            self.snapshot(retention=10).run()
        self.assertEqual(self.contents(),b'new\n')
        self.assertEqual(self.manifest()['retention_discarded_bytes'],4)

    def test_rotation_gap_before_writer_reopens_active(self):
        rotate_syslog.rotate(self.source)
        self.snapshot().run()
        self.assertEqual(self.contents(),b'first\n')

    def test_fragment_count_limits_small_events(self):
        self.active.write_bytes(b'')
        for i in range(130):
            with self.active.open('ab') as out: out.write(b'x\n')
            self.snapshot().run()
        self.assertEqual([len(v['files']) for v in self.manifest()['volumes']], [128,2])
        self.assertEqual(self.contents(), b'x\n'*130)

    def test_sync_failure_keeps_offsets_and_retry_removes_orphans(self):
        before = self.snapshot().run()
        with self.active.open('ab') as out: out.write(b'-end\n')
        snap = self.snapshot()
        def fail(_): raise OSError('writeback failure')
        snap.flush=fail
        with self.assertRaises(OSError): snap.run()
        self.assertEqual(self.manifest(), before)
        self.snapshot().run()
        self.assertEqual(self.contents(), b'first\npartial-end\n')
        files={str(p.relative_to(snap.root)) for p in (snap.root/'rsyslog').iterdir()}
        self.assertEqual(files, set(logs.committed_files(self.manifest())))

    def test_manifest_staging_failure_preserves_prior_commit(self):
        before = self.snapshot().run(); snap = self.snapshot()
        def fail(path):
            if path.name.startswith('.snapshot-'): raise OSError('manifest flush failure')
        snap.flush=fail
        with self.assertRaises(OSError): snap.run()
        self.assertEqual(self.manifest(), before)

    def test_prior_journal_is_retired_only_after_success(self):
        root = self.disk/'system_journal'; root.mkdir(); native=root/('a'*32); native.mkdir()
        (native/'system@old.journal').write_bytes(b'old')
        before={'schema':1,'format':'native-journal-only','journals':[{'path':'a'*32+'/system@old.journal'}]}
        (root/'manifest.json').write_text(json.dumps(before)); snap=self.snapshot()
        def fail(_): raise OSError('failure')
        snap.flush=fail
        with self.assertRaises(OSError):snap.run()
        self.assertTrue((native/'system@old.journal').exists())
        self.assertEqual(self.manifest(), before)
        self.snapshot().run()
        self.assertFalse((native/'system@old.journal').exists())
        self.assertNotIn('journals', self.manifest())

    def test_source_shrink_and_oversize_do_not_commit(self):
        snap=self.snapshot();original=snap.sources
        def shrink(stack):
            result=original(stack);self.active.write_bytes(b'');return result
        snap.sources=shrink
        with self.assertRaisesRegex(RuntimeError,'shrank'):snap.run()
        self.assertFalse((snap.root/'manifest.json').exists())
        self.active.write_bytes(b'x'*(128*1024+1)+b'\n')
        with self.assertRaisesRegex(RuntimeError,'128KiB'):self.snapshot().run()

    def test_missing_source_fails(self):
        self.active.unlink()
        with self.assertRaisesRegex(RuntimeError,'unavailable'):self.snapshot().run()

    def test_missing_committed_file_fails(self):
        m=self.snapshot().run()
        (self.disk/'system_journal'/logs.committed_files(m)[0]).unlink()
        with self.assertRaisesRegex(RuntimeError,'missing'):self.snapshot().run()

    def test_mount_identity_and_symlink_rejected(self):
        snap=self.snapshot();snap.require_mount=True
        with self.assertRaisesRegex(RuntimeError,'mount point'):snap.run()
        snap=self.snapshot();snap.check_disk();snap.identity=(0,0,'')
        with self.assertRaisesRegex(RuntimeError,'changed'):snap.check_disk()
        (self.disk/'system_journal').symlink_to(self.source)
        with self.assertRaisesRegex(RuntimeError,'symlink'):self.snapshot().run()

    def test_eviction_counter_and_removed_offsets(self):
        self.snapshot().run()
        self.active.rename(self.source/'syslog.00001.log')
        self.active.write_bytes(b'new\n');self.snapshot().run()
        (self.source/'syslog.00001.log').unlink()
        (self.source/'dropped_bytes').write_text('123')
        m=self.snapshot().run()
        self.assertEqual(m['ram_evicted_bytes'],123)
        self.assertEqual(len(m['offsets']),1)

    def test_foreground_sync_checked_without_timeout(self):
        with patch.object(logs.subprocess,'run') as run:logs.syncfs(self.disk)
        self.assertEqual(run.call_args.args[0],['/usr/bin/sync','-f',str(self.disk)])
        self.assertEqual(run.call_args.kwargs,{'check':True})

    def test_export_waits_for_prior_owner(self):
        lock=self.base/'export.lock'
        child=subprocess.Popen([sys.executable,'-c',
            'import fcntl,sys; f=open(sys.argv[1],"a");fcntl.flock(f,fcntl.LOCK_EX);print("locked",flush=True);sys.stdin.read(1)',str(lock)],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        self.assertEqual(child.stdout.readline().strip(),'locked'); errors=[]
        def export():
            try:logs.run_locked(self.disk,lock_path=str(lock),source=self.source,machine='a'*32,boot='boot-a',require_mount=False,flush=self.flushes.append)
            except Exception as exc:errors.append(exc)
        thread=threading.Thread(target=export);thread.start()
        try:
            time.sleep(.05);self.assertFalse((self.disk/'system_journal/manifest.json').exists())
            child.stdin.write('x');child.stdin.flush();child.wait(timeout=3);thread.join(timeout=3)
            self.assertFalse(thread.is_alive());self.assertEqual(errors,[])
        finally:
            if child.poll() is None:child.kill();child.wait()
            child.stdin.close();child.stdout.close();thread.join(timeout=3)

    def test_ram_rotation_bounded(self):
        for index in range(5):
            self.active.write_bytes(bytes([index])*10)
            rotate_syslog.rotate(self.source,limit=65536+30,chunk=10)
        self.assertLessEqual(sum(p.stat().st_size for p in self.source.glob('syslog.*.log')),20)
        self.assertEqual(int((self.source/'dropped_bytes').read_text()),30)


class SnapshotWorkerTests(unittest.TestCase):
    def test_event_only_reuse_failure_recovery_and_durable_ack(self):
        import socket
        entered=threading.Event(); release=threading.Event(); calls=[]
        def snapshot(disk):
            calls.append(disk)
            if disk=="bad": raise OSError("disk unavailable")
            entered.set()
            if not release.wait(2): raise RuntimeError("flush not released")
            return {"copied_bytes":9}
        parent,child=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        with patch.object(logs,"run_locked",side_effect=snapshot):
            worker=threading.Thread(target=logs.serve,args=(child.detach(),))
            worker.start(); parent.settimeout(2)
            self.assertTrue(json.loads(parent.recv(65536))["ready"])
            self.assertEqual(calls,[])
            parent.send(json.dumps({"disk":"bad"}).encode())
            self.assertFalse(json.loads(parent.recv(65536))["ok"])
            parent.send(json.dumps({"disk":"good"}).encode())
            self.assertTrue(entered.wait(1))
            parent.settimeout(.03)
            with self.assertRaises(TimeoutError): parent.recv(65536)
            release.set(); parent.settimeout(2)
            self.assertEqual(json.loads(parent.recv(65536)),{"ok":True,"copied_bytes":9})
            parent.send(json.dumps({"disk":"good"}).encode())
            self.assertTrue(json.loads(parent.recv(65536))["ok"])
            self.assertEqual(calls,["bad","good","good"])
            parent.close(); worker.join(2); self.assertFalse(worker.is_alive())


if __name__=='__main__':unittest.main()
