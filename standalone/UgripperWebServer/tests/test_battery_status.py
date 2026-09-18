import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import telemetry


class BatteryStatusTests(unittest.TestCase):
    def test_runtime_snapshot_refresh_and_disconnect(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'battery.json'
            with patch.object(telemetry, 'BATTERY_STATUS', path), patch('telemetry.time.time', return_value=100):
                self.assertEqual(telemetry.battery_status(), {'connected': False, 'status': 'service_unavailable'})
                for percent in (100, 5, 0):
                    path.write_text(json.dumps({'connected': True, 'percent': percent, 'updated_at_ms': 100000}))
                    self.assertEqual(telemetry.battery_status()['percent'], percent)
                for value in ([], {'percent': 0}, {'connected': False, 'percent': 50, 'updated_at_ms': 100000},
                              {'connected': True, 'percent': 50, 'updated_at_ms': 94999},
                              {'connected': True, 'percent': 50, 'updated_at_ms': 100001},
                              {'connected': True, 'percent': True, 'updated_at_ms': 100000},
                              {'connected': True, 'percent': 101, 'updated_at_ms': 100000}):
                    path.write_text(json.dumps(value))
                    self.assertEqual(telemetry.battery_status(), {'connected': False, 'status': 'service_unavailable'})


if __name__ == '__main__':
    unittest.main()
