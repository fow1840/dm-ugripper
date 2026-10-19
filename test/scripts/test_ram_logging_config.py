#!/usr/bin/env python3
"""Configuration transactions against an isolated tree and failing system tools."""
import base64
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/logging'))
import ram_logging_config as cfg


class ConfigTransactionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.members = {'ubuntu'}
        self.active = {'systemd-journald.service', 'rsyslog.service', 'log-persist.timer'}
        self.calls = []
        self.failure = None
        self.override = False
        self.config = cfg.Configuration(self.root, self.run_command)
        self.old = cfg.file_data('original journal config\n', 0o640)
        self.config.publish(cfg.JOURNAL, self.old)
        self.config.publish('/etc/systemd/system/log-persist.timer', {'kind': 'symlink', 'target': '/lib/systemd/system/log-persist.timer'})
        self.config.path('/tmp/logbuf').mkdir(parents=True)
        self.config.path('/tmp/logbuf/syslog.log').touch()
        self.config.path('/var/log').mkdir(parents=True)
        self.config.path('/var/log/journal').symlink_to('/mnt/data_disk/old-journal')
        self.initial = {key: self.config.read(key) for key in [*cfg.desired('ubuntu'), '/var/log/journal']}

    def run_command(self, args, check=True):
        self.calls.append(args)
        if self.failure and self.failure(args):
            self.failure = None
            if check:
                raise RuntimeError('injected tool failure: ' + str(args))
            return subprocess.CompletedProcess(args, 1, '', 'injected')
        out = ''; rc = 0
        if args[0].endswith('/id') and '-nG' in args:
            out = ' '.join(sorted(self.members))
        elif args[0].endswith('/findmnt'):
            out = 'tmpfs\n'
        elif args[0].endswith('/usermod'):
            self.members.update(args[3].split(','))
        elif args[0].endswith('/gpasswd'):
            self.members.discard(args[-1])
        elif args[0].endswith('/systemctl'):
            if args[1] == 'is-active': rc = int(args[-1] not in self.active)
            elif args[1] in ('restart', 'try-restart'): self.active.add(args[-1])
            elif args[1] == 'stop': self.active.discard(args[-1])
            elif args[1] == 'show': out = '-f /etc/ugripper/rsyslog-ram.conf'
        elif args[0].endswith('/systemd-analyze'):
            out = base64.b64decode(cfg.desired('ubuntu')[cfg.JOURNAL]['data']).decode()
            if self.override: out += '\n[Journal]\nStorage=persistent\n'
        if check and rc: raise RuntimeError('not active')
        return subprocess.CompletedProcess(args, rc, out, '')

    def assert_restored(self):
        self.assertEqual({key: self.config.read(key) for key in self.initial}, self.initial)
        self.assertEqual(self.members, {'ubuntu'})
        self.assertIn('log-persist.timer', self.active)
        self.assertIsNone(self.config.load('pending.json'))

    def test_success_reinstall_and_remove_restore_original_files_groups_masks(self):
        self.config.install('ubuntu')
        self.assertEqual(self.config.read(cfg.JOURNAL), cfg.desired('ubuntu')[cfg.JOURNAL])
        self.assertEqual(self.members, {'ubuntu', 'adm', 'systemd-journal'})
        original = self.config.load('original.json')
        inode = self.config.path(cfg.JOURNAL).stat().st_ino
        self.config.install('ubuntu')
        self.assertEqual(self.config.path(cfg.JOURNAL).stat().st_ino, inode)
        self.assertEqual(self.config.load('original.json'), original)
        self.config.uninstall()
        self.assert_restored()
        self.assertIsNone(self.config.load('original.json'))

    def test_invalid_candidate_does_not_change_live_policy(self):
        self.failure = lambda a: a[0].endswith('rsyslogd')
        with self.assertRaises(RuntimeError): self.config.install('ubuntu')
        self.assert_restored()
        self.assertFalse(self.config.state.exists())

    def test_non_tmpfs_aborts_before_configuration_mutation(self):
        real = self.config.runner
        def runner(args, check=True):
            if args[0].endswith('findmnt'): return subprocess.CompletedProcess(args, 0, 'ext4\n', '')
            return real(args, check=check)
        self.config.runner = runner
        with self.assertRaisesRegex(RuntimeError, 'tmpfs'): self.config.install('ubuntu')
        self.assert_restored()

    def test_service_failure_rolls_back_every_file_group_and_service(self):
        self.failure = lambda a: a[0].endswith('systemctl') and a[1:] == ['restart', 'rsyslog.service']
        with self.assertRaisesRegex(RuntimeError, 'injected'): self.config.install('ubuntu')
        self.assert_restored()
        self.assertIsNone(self.config.load('original.json'))

    def test_later_journald_override_is_not_silently_accepted(self):
        self.override = True
        with self.assertRaisesRegex(RuntimeError, 'overridden'): self.config.install('ubuntu')
        self.assert_restored()

    def test_publication_failure_rolls_back_and_keeps_old_commit(self):
        self.config.install('ubuntu')
        before = self.config.load('original.json')
        original_publish = self.config.publish
        def failing(name, value):
            if name == cfg.STATE + '/pending.json' and value['kind'] == 'absent':
                self.config.publish = original_publish
                raise OSError('writeback error after original metadata publication')
            return original_publish(name, value)
        self.config.publish = failing
        with self.assertRaises(OSError): self.config.install('ubuntu')
        self.assertEqual(self.config.load('original.json'), before)
        self.config.uninstall(); self.assert_restored()

    def test_sigkill_durable_undo_record_recovers_on_next_run(self):
        # os._exit simulation bypasses Python rollback entirely.
        class AbruptExit(BaseException): pass
        original_restore = self.config.restore_pending
        original_publish = self.config.publish
        def interrupted(name, value):
            original_publish(name, value)
            if name == cfg.RSYSLOG:
                raise AbruptExit()
        self.config.publish = interrupted
        def no_rollback(*args, **kwargs):
            if self.config.load('pending.json'): raise AbruptExit()
            return original_restore(*args, **kwargs)
        self.config.restore_pending = no_rollback
        with self.assertRaises(AbruptExit): self.config.install('ubuntu')
        self.assertIsNotNone(self.config.load('pending.json'))
        recovered = cfg.Configuration(self.root, self.run_command)
        recovered.restore_pending()
        self.assert_restored()

    def test_external_change_is_preserved_and_removal_fails_closed(self):
        self.config.install('ubuntu')
        external = cfg.file_data('administrator edit\n')
        self.config.publish(cfg.RSYSLOG, external)
        for action in (lambda: self.config.install('ubuntu'), self.config.uninstall):
            with self.assertRaisesRegex(RuntimeError, 'external'): action()
            self.assertEqual(self.config.read(cfg.RSYSLOG), external)

    def test_same_directory_atomic_replace_never_exposes_partial_live_file(self):
        seen = []
        original = os.replace
        def replace(source, dest):
            if Path(dest) == self.config.path(cfg.JOURNAL):
                seen.append(Path(dest).read_bytes())
                self.assertEqual(Path(source).parent, Path(dest).parent)
                self.assertEqual(Path(source).read_bytes(), b'new complete\n')
            return original(source, dest)
        with patch.object(cfg.os, 'replace', side_effect=replace):
            self.config.publish(cfg.JOURNAL, cfg.file_data('new complete\n'))
        self.assertEqual(seen, [b'original journal config\n'])

    def test_boot_recovery_does_not_deadlock_on_ordered_rsyslog(self):
        pending = {'user': 'ubuntu', 'before': self.initial, 'added_groups': [],
                   'active': list(self.active)}
        self.config.save('pending.json', pending)
        self.calls.clear()
        self.config.restore_pending(boot=True)
        self.assertFalse(any(a[1:3] == ['restart', 'rsyslog.service'] for a in self.calls))
        self.assert_restored()

    def test_uninstall_restart_failure_returns_to_installed_policy(self):
        self.config.install('ubuntu')
        installed = {key: self.config.read(key) for key in self.initial}
        marker = self.config.load('original.json')
        self.failure = lambda a: a[0].endswith('systemctl') and a[1:] == ['restart', 'rsyslog.service']
        with self.assertRaises(RuntimeError): self.config.uninstall()
        self.assertEqual({key: self.config.read(key) for key in self.initial}, installed)
        self.assertEqual(self.config.load('original.json'), marker)
        self.assertEqual(self.members, {'ubuntu', 'adm', 'systemd-journal'})
        self.config.uninstall(); self.assert_restored()

    def test_file_data_flush_failure_never_replaces_live_config(self):
        before = self.config.read(cfg.JOURNAL)
        with patch.object(cfg.os, 'fsync', side_effect=OSError('data flush failed')):
            with self.assertRaises(OSError):
                self.config.publish(cfg.JOURNAL, cfg.file_data('replacement'))
        self.assertEqual(self.config.read(cfg.JOURNAL), before)
        self.assertEqual(list(self.config.path(cfg.JOURNAL).parent.glob('.ugripper-config-*')), [])

    def test_each_file_publication_failure_preserves_initial_policy(self):
        # Includes recovery setup, retired files, symlinks and commit metadata.
        paths = [*cfg.desired('ubuntu'), '/var/log/journal', cfg.STATE + '/pending.json', cfg.STATE + '/original.json']
        for target in paths:
            with self.subTest(target=target):
                original_publish = self.config.publish
                failed = False
                def fail_once(name, value):
                    nonlocal failed
                    if name == target and not failed:
                        failed = True
                        raise OSError('injected publication failure')
                    return original_publish(name, value)
                self.config.publish = fail_once
                try:
                    with self.assertRaises(OSError): self.config.install('ubuntu')
                finally:
                    self.config.publish = original_publish
                self.assert_restored()
                self.assertIsNone(self.config.load('original.json'))

    def test_writable_state_is_not_trusted_for_root_recovery(self):
        self.config.save('pending.json', {'bad': 'untrusted'})
        self.config.state.chmod(0o777)
        with self.assertRaisesRegex(RuntimeError, 'owned and writable'):
            self.config.restore_pending()

    def test_parent_symlink_rejected(self):
        parent = self.config.path('/etc/sudoers.d')
        parent.symlink_to(self.root/'outside')
        with self.assertRaisesRegex(RuntimeError, 'symlink'): self.config.publish(cfg.SUDOERS, cfg.file_data('bad'))


if __name__ == '__main__':
    unittest.main()
