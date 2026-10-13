import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import telemetry


class DeviceVersionsTest(unittest.TestCase):
    def test_reuses_firmware_cache_and_retains_existing_firmware_view(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'firmware_status.json'
            original = {'main_camera': {'left': {'iserial': 'camera-label'}},
                        'gripper': {'left': {'version': 'V1.0.5'}},
                        'updated_at_ms': time.time()*1000,
                        'device_versions': {'hardware_version': 'v2.5.0',
                                            'cam_chest_firmware_version': 'disabled',
                                            'stereo_left_firmware_version': '1.2.5',
                                            'invalid': 123}}
            path.write_text(json.dumps(original))
            with patch.object(telemetry, 'FIRMWARE_STATUS', path):
                view = telemetry.firmware_status()
                self.assertEqual(view['main_camera']['left'], original['main_camera']['left'])
                self.assertEqual(view['gripper']['left'], original['gripper']['left'])
                versions = telemetry.device_versions()
                self.assertEqual(versions['stereo_left_firmware_version'], '1.2.5')
                self.assertEqual(versions['cam_chest_firmware_version'], 'disabled')
                self.assertNotIn('invalid', versions)

    def test_stale_malformed_and_old_firmware_snapshots_are_unknown(self):
        for data in ({}, [], {'updated_at_ms': 1, 'device_versions': {'hardware_version': 'old'}},
                     {'updated_at_ms': True}, {'updated_at_ms': time.time()*1000, 'device_versions': []}):
            with patch.object(telemetry, 'read_json', return_value=data):
                self.assertEqual(telemetry.device_versions(), {})


if __name__ == '__main__':
    unittest.main()
