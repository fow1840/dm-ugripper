import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ego_status import ego_status


class EgoStatusTests(unittest.TestCase):
    def test_battery_requires_connected_matching_fresh_snapshot(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'ego_binding.json').write_text(json.dumps({'serial': 'ego'}))
            for state, serial, stamp, percent, valid in [
                ('connected', 'ego', 80000, 0, True),
                ('connected', 'ego', 79999, 50, False),
                ('connected', 'ego', 100001, 50, False),
                ('connected', 'ego', 100000, True, False),
                ('disconnected', 'ego', 100000, 50, False),
                ('connected', 'other', 100000, 50, False)]:
                (root / 'ego_binding_status.json').write_text(json.dumps(dict(
                    status=state, serial=serial, updated_at_ms=100000,
                    battery=dict(percent=percent, updated_at_ms=stamp, charging=True))))
                self.assertEqual('battery' in ego_status(root, 100000), valid)

    def test_binding_is_authoritative_and_probe_must_be_current(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            binding = root / 'ego_binding.json'
            probe = root / 'ego_binding_status.json'
            probe.write_text(json.dumps(dict(status='connected', serial='old', updated_at_ms=100000)))
            self.assertEqual(ego_status(root, 100000)['state'], 'unbound')
            binding.write_text(json.dumps(dict(serial='new')))
            self.assertIsNone(ego_status(root, 100000)['connected'])
            for state, expected in [('connected', True), ('disconnected', False), ('bound', None)]:
                probe.write_text(json.dumps(dict(status=state, serial='new', updated_at_ms=100000)))
                value = ego_status(root, 100000)
                self.assertTrue(value['bound'])
                self.assertIs(value['connected'], expected)
            probe.write_text(json.dumps(dict(status='connected', serial='new', updated_at_ms=100000)))
            for now in (115001, 99999):
                self.assertIsNone(ego_status(root, now)['connected'])
            for bad in ('{', '[]', 'null'):
                probe.write_text(bad)
                self.assertIsNone(ego_status(root, 100000)['connected'])
            binding.write_text('{')
            self.assertIsNone(ego_status(root, 100000)['bound'])


if __name__ == '__main__':
    unittest.main()
