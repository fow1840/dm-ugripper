"""Version failures require an explicit decision; other DFU checks remain strict."""
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import maintenance
from production import main_camera_dfu as dfu


class VersionConfirmationTest(unittest.TestCase):
    def test_wait_accept_cancel_timeout_and_stale_token(self):
        for choice in (True, False, None):
            with self.subTest(choice=choice), tempfile.TemporaryDirectory() as temporary:
                published = threading.Event()
                task = {'id': 'task'}
                def report(stage, **fields):
                    task.update(fields)
                    if fields.get('confirmation'):
                        published.set()
                outcome = []
                def wait():
                    try:
                        outcome.append(maintenance.confirm_camera_version_failure(
                            'left', 'before_flash', 'XU unavailable', timeout=0 if choice is None else 2))
                    except ValueError as exc:
                        outcome.append(str(exc))
                with patch.object(maintenance, 'JOB', task), \
                     patch.object(maintenance, 'PRIVATE', Path(temporary)), \
                     patch.object(maintenance, 'report', report):
                    worker = threading.Thread(target=wait)
                    worker.start()
                    self.assertTrue(published.wait(1))
                    if choice is not None:
                        request = {'id': 'task', 'token': task['confirmation']['token'], 'accepted': choice}
                        with self.assertRaises(ValueError):
                            maintenance.answer_camera_version_failure(dict(request, token='stale'))
                        maintenance.answer_camera_version_failure(request)
                    worker.join(3)
                    self.assertFalse(worker.is_alive())
                    self.assertEqual(outcome == [True], choice is True)
                    self.assertIsNone(task.get('confirmation'))
                    self.assertFalse(maintenance.VERSION_CONFIRM)

    def test_upgrade_failures_require_confirmation_and_keep_other_checks(self):
        for stage, accept, post_version in (
            ('before_flash', True, '2026-09-02-0001'),
            ('before_flash', False, '2026-09-02-0001'),
            ('after_flash', True, None),
            ('after_flash', False, None),
            ('before_flash', True, '2026-09-01-0001'),
        ):
            with self.subTest(stage=stage, accept=accept, post_version=post_version), \
                 tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                image = root/'camera.img'; image.write_bytes(b'firmware')
                tools = []
                for name in ('reader', 'trigger', 'dfu-util'):
                    tool = root/name; tool.write_text('#!/bin/sh\n'); tool.chmod(0o755); tools.append(tool)
                failure = dfu.DfuError('device_version_read_failed', 'XU unavailable')
                reads = iter((failure if stage == 'before_flash' else '2026-09-01-0001',
                              failure if stage == 'after_flash' else post_version))
                def read(side):
                    value = next(reads)
                    if isinstance(value, Exception): raise value
                    return value
                def runner(command, **kwargs):
                    return subprocess.CompletedProcess(command, 0,
                        '待升级固件版本: 2026-09-02-0001\n' if command[0] == str(tools[0]) else 'Done!\n', '')
                with patch.object(maintenance, 'camera_regression', return_value={'ok': True}) as regression:
                    updater = dfu.MainCameraDfuUpdater(
                        firmware_dir=root, version_reader=tools[0], dfu_trigger=tools[1], dfu_util=tools[2],
                        runner=runner, locate_uvc_port=lambda side: '9-1.2.1',
                        enumerate_dfu=lambda: [dfu.DfuDevice('9-1.2.1', alt=0)],
                        camera_present=lambda side: True, read_device_version=read,
                        regression_check=lambda side: regression({}, {}))
                    decisions = []
                    def confirm(side, phase, error):
                        decisions.append((side, phase))
                        return accept
                    if not accept or post_version == '2026-09-01-0001':
                        with self.assertRaises(dfu.DfuError):
                            updater.upgrade('left', image, force=True, confirm_version_failure=confirm)
                        regression.assert_not_called()
                    else:
                        result = updater.upgrade('left', image, confirm_version_failure=confirm)
                        self.assertFalse(result.skipped)
                        self.assertEqual(result.regression['version_verification_skipped'], stage == 'after_flash')
                        regression.assert_called_once()
                    self.assertEqual(decisions, [('left', stage)])


if __name__ == '__main__':
    unittest.main()
