from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import telemetry


class BatteryStatusTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.path = Path(self.folder.name) / 'bms.txt'
        for patcher in (patch.object(telemetry, 'BMS_SOURCE', self.path),
                        patch('telemetry.time.time', return_value=100)):
            patcher.start()
            self.addCleanup(patcher.stop)
        self.fields = dict(timestamp=100, soc_percent=100, totalCapacity_mAh=5000,
                           remainCapacity_mAh=5000, totalVoltage_mV=28922,
                           current_mA=-342, isCharging=0, isDischarging=1)

    def write(self, **changes):
        self.path.write_text(''.join(f'{k}: {v}\n' for k, v in (self.fields | changes).items()))

    def test_valid_without_runtime_and_percent_boundaries(self):
        for percent in (0, 5, 100):
            self.write(soc_percent=percent)
            value = telemetry.battery_status()
            self.assertTrue(value['connected'])
            self.assertEqual(value['percent'], percent)
            self.assertEqual(value['current_ma'], -342)
            self.assertTrue(value['discharging'])

    def test_freshness_boundary_and_recovery(self):
        self.write(timestamp=80)
        self.assertTrue(telemetry.battery_status()['connected'])
        with patch('telemetry.time.time', return_value=100.001):
            self.assertFalse(telemetry.battery_status()['connected'])
        self.write(timestamp=101)
        self.assertFalse(telemetry.battery_status()['connected'])
        self.write()
        self.assertTrue(telemetry.battery_status()['connected'])

    def test_missing_truncated_duplicate_and_oversized(self):
        self.assertFalse(telemetry.battery_status()['connected'])
        for content in ('timestamp: 100\nsoc_percent: 100\n', 'x'*4097,
                        ''.join(f'{k}: {v}\n' for k,v in self.fields.items()) + 'timestamp: 100\n'):
            self.path.write_text(content)
            self.assertFalse(telemetry.battery_status()['connected'])
        self.path.write_bytes(b'\xff')
        self.assertFalse(telemetry.battery_status()['connected'])

    def test_invalid_numeric_and_boolean_fields(self):
        for key, value in [('soc_percent', 101), ('soc_percent', -1), ('soc_percent', '1.0'),
                           ('soc_percent', '1_0'), ('isCharging', 2), ('isDischarging', -1),
                           ('current_mA', 2147483648), ('totalCapacity_mAh', -1), ('timestamp', 0)]:
            with self.subTest(key=key, value=value):
                self.write(**{key: value})
                self.assertFalse(telemetry.battery_status()['connected'])


if __name__ == '__main__':
    unittest.main()
