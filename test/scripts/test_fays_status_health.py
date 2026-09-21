"""Exercise the daemon's actual status producer with deterministic clocks."""
import pathlib
import unittest
from unittest.mock import patch
import os

SOURCE = (pathlib.Path(__file__).resolve().parents[2] /
          'standalone/FaysStereoRecorder/scripts/run_fays_stereo_daemon.sh').read_text()
CODE = SOURCE.split("python3 - <<'PY'", 1)[1].split('status_file = os.environ["STATUS_FILE"]', 1)[0]


class StatusHealthTest(unittest.TestCase):
    def setUp(self):
        self.env = patch.dict(os.environ, STATUS_FILE='/nonexistent/fays-test',
                              DAEMON_STARTED_MS='100', LEFT_PID='42',
                              FRAME_STALE_SEC='3', IMU_STALE_SEC='3', RECORDING='false')
        self.env.start()
        self.addCleanup(self.env.stop)
        self.scope = {}
        exec(CODE, self.scope)
        self.calib = dict(valid=True, serial_number='serial', devices=dict(
            stereo_dev_port='/dev/video0', imu_dev_port='/dev/video4'))
        self.runtime = dict(valid=True, running=True, pid=42,
                            status_write_system_ns=100_000_000_000,
                            last_frame_system_ns=100_000_000_000,
                            last_imu_system_ns=100_000_000_000)

    def state(self, t, **changes):
        with patch('os.path.exists', return_value=True), patch('time.time_ns', return_value=100_000_000_000), patch('time.monotonic', return_value=t):
            result = self.scope['camera_state']('left_stereo', True, 'config', self.calib,
                                                dict(self.runtime, **changes))
        self.scope['previous'] = {'cameras': {'left_stereo': result}}
        return result

    def test_no_frames_never_ready_and_retries_do_not_extend_grace(self):
        self.assertTrue(self.state(10, last_frame_system_ns=0)['recovery_pending'])
        os.environ['LEFT_PID'] = '43'
        self.assertTrue(self.state(25, pid=43, last_frame_system_ns=0)['recovery_pending'])
        result = self.state(31, pid=43, last_frame_system_ns=0)
        self.assertFalse(result['ready'])
        self.assertFalse(result['recovery_pending'])
        self.assertEqual(result['state'], 'warmup frame stale')

    def test_fresh_streams_recover_and_reset_deadline(self):
        self.state(10, last_frame_system_ns=0)
        self.assertTrue(self.state(15)['ready'])
        self.assertTrue(self.state(40, last_imu_system_ns=0)['recovery_pending'])

    def test_stale_imu_status_and_old_pid_are_not_ready(self):
        for changes in [dict(last_imu_system_ns=0), dict(last_frame_system_ns=96_000_000_000),
                        dict(status_write_system_ns=96_000_000_000), dict(pid=41)]:
            with self.subTest(changes=changes):
                self.assertFalse(self.state(10, **changes)['ready'])

    def test_recording_does_not_hide_stream_loss(self):
        os.environ['RECORDING'] = 'true'
        self.assertFalse(self.state(10, last_frame_system_ns=0)['recovery_pending'])

    def test_remapped_device_gets_new_grace(self):
        self.state(10, last_frame_system_ns=0)
        self.assertFalse(self.state(31, last_frame_system_ns=0)['recovery_pending'])
        self.calib['devices']['stereo_dev_port'] = '/dev/video8'
        self.assertTrue(self.state(32, last_frame_system_ns=0)['recovery_pending'])


if __name__ == '__main__':
    unittest.main()
