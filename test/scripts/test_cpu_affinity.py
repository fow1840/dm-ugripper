#!/usr/bin/env python3
"""Check profile fallback preserves administrator restrictions."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('cpu_affinity', Path(__file__).resolve().parents[2] / 'config/cpu_affinity.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class CpuAffinityTest(unittest.TestCase):
    def topology(self, directory):
        for cpu in range(8):
            p = Path(directory) / f'devices/system/cpu/cpu{cpu}/cpu_capacity'
            p.parent.mkdir(parents=True)
            p.write_text('512' if cpu < 4 else '1024')

    def test_off_does_not_touch_affinity_or_environment(self):
        with patch.object(m.os, 'sched_setaffinity') as change, patch.dict(os.environ, {}, clear=True):
            m.configure('off', Path('/does-not-exist'))
            change.assert_not_called()
            self.assertNotIn('UGRIPPER_CPU_BASE_MASK', os.environ)

    def test_administrator_restriction_is_not_widened(self):
        with tempfile.TemporaryDirectory() as root:
            self.topology(root)
            with patch.object(m.os, 'sched_getaffinity', return_value={0, 1, 4, 5}), patch.object(m.os, 'sched_setaffinity') as change, patch.dict(os.environ, {}, clear=True):
                with self.assertRaises(ValueError):
                    m.configure('fays_split', Path(root))
                change.assert_not_called()
                self.assertNotIn('UGRIPPER_CPU_BASE_MASK', os.environ)

    def test_failed_apply_does_not_publish_permission_to_widen(self):
        with tempfile.TemporaryDirectory() as root:
            self.topology(root)
            with patch.object(m.os, 'sched_getaffinity', return_value=set(range(8))), patch.object(m.os, 'sched_setaffinity', side_effect=OSError('denied')), patch.dict(os.environ, {}, clear=True):
                with self.assertRaises(OSError):
                    m.configure('fays_split', Path(root))
                self.assertNotIn('UGRIPPER_CPU_BASE_MASK', os.environ)

    def test_success_exports_original_allowance(self):
        with tempfile.TemporaryDirectory() as root:
            self.topology(root)
            with patch.object(m.os, 'sched_getaffinity', return_value=set(range(8))), patch.object(m.os, 'sched_setaffinity') as change, patch.dict(os.environ, {}, clear=True):
                m.configure('fays_split', Path(root))
                change.assert_called_once_with(0, set(range(4)))
                self.assertEqual(os.environ['UGRIPPER_CPU_BASE_MASK'], 'ff')

    def test_cpu3_requires_online_isolated_and_can_be_disabled(self):
        for isolated, online, enabled, expected in [
                ('3-7', '0-7', 'true', True),
                ('4-7', '0-7', 'true', False),
                ('3-7', '0-2,4-7', 'true', False),
                ('3-7', '0-7', 'false', False),
                ('bad', '0-7', 'true', False)]:
            with self.subTest(isolated=isolated, online=online, enabled=enabled), tempfile.TemporaryDirectory() as root:
                self.topology(root)
                cpu = Path(root) / 'devices/system/cpu'
                (cpu / 'isolated').write_text(isolated)
                (cpu / 'online').write_text(online)
                with patch.object(m.os, 'sched_getaffinity', return_value=set(range(8))), patch.object(m.os, 'sched_setaffinity') as change, patch.dict(os.environ, {'UGRIPPER_ENCODER_CPU3': enabled}, clear=True):
                    m.configure('fays_split', Path(root))
                    change.assert_called_once_with(0, set(range(3 if expected else 4)))
                    self.assertEqual(os.environ['UGRIPPER_ENCODER_CPU3_ACTIVE'], '1' if expected else '0')


if __name__ == '__main__':
    unittest.main()
