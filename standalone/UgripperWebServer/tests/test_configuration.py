import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import environment_config as config
import maintenance


class ConfigurationTests(unittest.TestCase):
    def setUp(self):
        self.values = {key: choices[0] for key, choices in config.OPTIONS.items()}

    def test_invalid_and_injected_values_are_rejected_before_writing(self):
        for changes in ({'PATH': '/tmp'}, {'UGRIPPER_LANG': 'zh\nPATH=/tmp'}, {'CAMERA_CODEC': '$(id)'}, {'ENABLE_CHEST_CAM_MAIN': True}, {'UGRIPPER_POWER_LINE_FREQUENCY': '59'}):
            values = self.values | changes
            with patch('environment_config.os.open') as opened, self.assertRaises(ValueError):
                config.write_settings(values)
            opened.assert_not_called()

    def test_atomic_update_preserves_other_settings_and_removes_duplicates(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'environment'
            path.write_text('# keep comment\nDEVICE_SN="abc"\nexport UGRIPPER_LANG = "en" # old\nUGRIPPER_LANG=zh\nPATH="/bin:/usr/bin"')
            path.chmod(0o640)
            with patch.object(config, 'ENVIRONMENT', path), patch('os.geteuid', return_value=0), patch('os.fchown'):
                config.write_settings(self.values)
                self.assertEqual(config.read_settings(), self.values)
                first = path.read_text()
                config.write_settings(self.values)
                self.assertEqual(path.read_text(), first)
            self.assertIn('DEVICE_SN="abc"\n', first)
            self.assertIn('PATH="/bin:/usr/bin"\n', first)
            self.assertTrue(first.startswith('# keep comment\n'))
            self.assertEqual(first.count('UGRIPPER_LANG='), 1)
            self.assertEqual(path.stat().st_mode & 0o777, 0o640)

    def test_single_side_zeroing_cannot_dispatch_both(self):
        for side in ('left', 'right'):
            with patch('maintenance.Path.exists', return_value=True), patch.object(maintenance, 'report'), patch.object(maintenance, 'run', return_value='[SUCCESS]') as run:
                self.assertEqual(maintenance.execute('zeroing', None, side=side), {side: 'success'})
                run.assert_called_once_with([maintenance.BASE/'bin/zeroing', side], 120)
        for side in (None, 'both', '../left'):
            with self.assertRaises(ValueError):
                maintenance.submit({'kind': 'zeroing', 'fixture': 'FIXTURE_READY', 'side': side})

    def test_nonroot_cannot_save(self):
        with patch('os.geteuid', return_value=1000), self.assertRaises(PermissionError):
            config.write_settings(self.values)

    def test_one_gripper_query_failure_does_not_hide_other_side(self):
        from subprocess import CompletedProcess
        with tempfile.TemporaryDirectory() as folder, patch.object(maintenance, 'PRIVATE', Path(folder)), patch.object(maintenance, 'JOB', {'id': 'test'}), patch.object(maintenance, 'report'), patch('maintenance.Path.exists', return_value=True), patch.object(maintenance, 'cache_gripper') as cache, patch.object(maintenance, 'command', side_effect=[CompletedProcess([], 0, '版本号：V1.0.5_20260820', ''), CompletedProcess([], 1, 'NACK', '')]):
            result = maintenance.execute('read_versions', None)
            self.assertEqual(result['left'], 'V1.0.5_20260820')
            self.assertEqual(result['right'], '未读取到版本')
            self.assertEqual(cache.call_args_list[0].args, ('left', 'V1.0.5_20260820'))
            self.assertEqual(cache.call_args_list[1].args, ('right', ''))


if __name__ == '__main__':
    unittest.main()
