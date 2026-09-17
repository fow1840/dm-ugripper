#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('uvc_affinity', ROOT / 'config/uvc_workqueue_affinity.py')
affinity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(affinity)


class WorkqueueAffinityTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.mask = self.root / 'devices/virtual/workqueue/cpumask'
        self.compatible = self.root / 'firmware/devicetree/base/compatible'
        self.state = self.root / 'state.json'
        for path, value in ((self.mask, 'ff\n'), (self.compatible, 'vendor,board\0rockchip,rk3588\0'),
                            (self.root / 'devices/system/cpu/online', '0-7')):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value)
        for cpu in range(8):
            path = self.root / ('devices/system/cpu/cpu' + str(cpu)) / 'cpu_capacity'
            path.parent.mkdir(parents=True)
            path.write_text('414' if cpu < 4 else '1024')

    def test_apply_is_idempotent_and_stop_restores_original(self):
        affinity.apply(self.root, self.state)
        self.assertEqual(self.mask.read_text().strip(), 'f0')
        affinity.apply(self.root, self.state)
        self.assertEqual(json.loads(self.state.read_text())['previous'], 255)
        affinity.restore(self.mask, self.state)
        self.assertEqual(self.mask.read_text().strip(), 'ff')
        self.assertFalse(self.state.exists())
        affinity.restore(self.mask, self.state)

    def test_existing_administrator_restriction_is_not_widened(self):
        self.mask.write_text('53')
        affinity.apply(self.root, self.state)
        self.assertEqual(self.mask.read_text().strip(), '50')
        affinity.restore(self.mask, self.state)
        self.assertEqual(self.mask.read_text().strip(), '53')

    def test_small_cores_only_restriction_is_preserved(self):
        self.mask.write_text('f')
        affinity.apply(self.root, self.state)
        self.assertEqual(self.mask.read_text(), 'f')
        self.assertFalse(self.state.exists())

    def test_external_change_is_preserved_on_stop(self):
        affinity.apply(self.root, self.state)
        self.mask.write_text('3f')
        affinity.restore(self.mask, self.state)
        self.assertEqual(self.mask.read_text(), '3f')
        self.assertFalse(self.state.exists())

    def test_external_change_is_preserved_on_repeated_start(self):
        affinity.apply(self.root, self.state)
        self.mask.write_text('3f')
        affinity.apply(self.root, self.state)
        self.assertEqual(self.mask.read_text(), '3f')
        self.assertFalse(self.state.exists())

    def test_disabled_setting_restores_an_applied_mask(self):
        affinity.apply(self.root, self.state)
        affinity.apply(self.root, self.state, enabled=False)
        self.assertEqual(self.mask.read_text().strip(), 'ff')
        self.assertFalse(self.state.exists())

    def test_other_soc_is_untouched(self):
        self.compatible.write_bytes(b'vendor,other\0')
        affinity.apply(self.root, self.state)
        self.assertEqual(self.mask.read_text().strip(), 'ff')
        self.assertFalse(self.state.exists())

    def test_homogeneous_cores_are_untouched(self):
        for path in self.root.glob('devices/system/cpu/cpu*/cpu_capacity'):
            path.write_text('1024')
        affinity.apply(self.root, self.state)
        self.assertEqual(self.mask.read_text().strip(), 'ff')
        self.assertFalse(self.state.exists())

    def test_missing_topology_does_not_modify_mask(self):
        (self.root / 'devices/system/cpu/cpu4/cpu_capacity').unlink()
        with self.assertRaises(OSError):
            affinity.apply(self.root, self.state)
        self.assertEqual(self.mask.read_text().strip(), 'ff')
        self.assertFalse(self.state.exists())

    def test_offline_big_core_is_excluded(self):
        (self.root / 'devices/system/cpu/online').write_text('0-5,7')
        affinity.apply(self.root, self.state)
        self.assertEqual(self.mask.read_text().strip(), 'b0')

    def test_interrupted_start_can_retry(self):
        self.state.write_text(json.dumps({'previous': 255, 'applied': 240}))
        affinity.apply(self.root, self.state)
        self.assertEqual(self.mask.read_text().strip(), 'f0')
        affinity.restore(self.mask, self.state)
        self.assertEqual(self.mask.read_text().strip(), 'ff')

    def test_failed_kernel_write_keeps_restore_state(self):
        original_write = Path.write_text
        def write(path, text, *args, **kwargs):
            if path == self.mask:
                raise PermissionError('simulated sysfs failure')
            return original_write(path, text, *args, **kwargs)
        with mock.patch.object(Path, 'write_text', write), self.assertRaises(PermissionError):
            affinity.apply(self.root, self.state)
        self.assertTrue(self.state.exists())
        affinity.restore(self.mask, self.state)
        self.assertEqual(self.mask.read_text().strip(), 'ff')
        self.assertFalse(self.state.exists())

    def test_kernel_mask_format_supports_multiple_words(self):
        for mask in (0xF0, 1 << 40, (1 << 80) | 15):
            text = affinity.mask_text(mask)
            self.assertTrue(all(len(word) <= 8 for word in text.strip().split(',')))
            self.assertEqual(affinity.mask_value(text), mask)


if __name__ == '__main__':
    unittest.main()
