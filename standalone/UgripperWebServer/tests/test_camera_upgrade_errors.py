import sys
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import maintenance


class CameraUpgradeErrorsTest(unittest.TestCase):
    def test_missing_left_precheck_does_not_read_image_or_flash(self):
        def exists(path):
            return str(path) != '/dev/cam_left'
        with patch.object(maintenance, 'report'), patch.object(Path, 'exists', exists), \
             patch.object(maintenance.dfu, 'read_image_version') as read_image, \
             patch.object(maintenance, 'camera_identity') as identity:
            with self.assertRaisesRegex(ValueError, '/dev/cam_left'):
                maintenance.execute('main_camera', Path('/firmware/valid.img'))
            read_image.assert_not_called()
            identity.assert_not_called()

    def test_device_version_error_retains_path_exit_code_stderr_and_log(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stderr = '/dev/cam_left: No such file or directory\n'
            result = subprocess.CompletedProcess([], 1, '', stderr)
            with patch.object(maintenance, 'PRIVATE', root), \
                 patch.object(maintenance, 'JOB', {'id': 'test'}), \
                 patch.object(maintenance, 'command', return_value=result) as command:
                with self.assertRaises(maintenance.dfu.DfuError) as raised:
                    maintenance.camera_version('left')
                self.assertEqual(raised.exception.code, 'device_version_read_failed')
                self.assertIn(stderr.strip(), raised.exception.message)
                self.assertEqual(raised.exception.details['returncode'], 1)
                self.assertEqual(raised.exception.details['stderr'], stderr)
                self.assertIn(stderr, (root/'test.log').read_text())
                command.assert_called_once_with([maintenance.BASE/'bin/linux_read_version', '--device', '/dev/cam_left'], 60)

    def test_device_version_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.CompletedProcess([], 0, '当前 UVC 摄像头版本 (/dev/cam_right): 2026-09-29-0341\n', '')
            with patch.object(maintenance, 'PRIVATE', Path(temporary)), \
                 patch.object(maintenance, 'JOB', {'id': 'test'}), \
                 patch.object(maintenance, 'command', return_value=result):
                self.assertEqual(maintenance.camera_version('right'), '2026-09-29-0341')
