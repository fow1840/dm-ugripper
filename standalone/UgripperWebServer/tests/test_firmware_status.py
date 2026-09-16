import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import telemetry


class FirmwareStatusTests(unittest.TestCase):
    def test_runtime_snapshot_is_only_source_and_stale_values_are_hidden(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'firmware_status.json'
            value = {'updated_at_ms': 100000, 'main_camera': {'left': {'iserial': 'camera-version'}},
                     'gripper': {'left': {'version': 'V1.0.5', 'status': 'ready'},
                                 'right': {'version': '', 'status': 'unavailable'}}}
            with patch.object(telemetry, 'FIRMWARE_STATUS', path), patch('telemetry.time.time', return_value=100), patch.object(telemetry, 'command') as command:
                path.write_text(json.dumps(value))
                result = telemetry.firmware_status()
                self.assertEqual(result['gripper']['left']['version'], 'V1.0.5')
                self.assertEqual(result['gripper']['right']['status'], 'unavailable')
                self.assertEqual(result['main_camera']['left']['iserial'], 'camera-version')
                command.assert_not_called()
                for bad in ({'updated_at_ms': 84000}, {'updated_at_ms': 100001}, [], {'gripper': []}):
                    path.write_text(json.dumps(bad))
                    self.assertEqual(telemetry.firmware_status()['gripper'], {'left': {}, 'right': {}})
                path.unlink()
                self.assertEqual(telemetry.firmware_status()['main_camera'], {'left': {}, 'right': {}})


if __name__ == '__main__':
    unittest.main()
