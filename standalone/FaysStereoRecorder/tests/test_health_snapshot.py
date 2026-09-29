#!/usr/bin/env python3
"""Health helper: stale/malformed input, recorder restart and helper recovery."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
SOURCE = (SCRIPTS / "run_fays_stereo_daemon.sh").read_text()
FUNCTIONS = SOURCE[SOURCE.index('HEALTH_HELPER_PID=""'):SOURCE.index('calibration_serial() {')]
ENV = dict(os.environ, PATH="/usr/bin:/bin")
ENV.pop("LD_LIBRARY_PATH", None)


class SnapshotTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name)
        self.cal = self.path / "cal.json"
        self.runtime = self.path / "runtime.json"
        self.write(self.cal, {"valid": True, "device_info": {"serial_number": "test"}})
        self.write_runtime()
        self.process = subprocess.Popen([
            "/usr/bin/python3", str(SCRIPTS / "fays_health_snapshot.py"),
            str(self.path), str(self.cal), str(self.runtime), str(self.cal),
            str(self.runtime), "3", "3"], env=ENV)
        self.wait_snapshot()

    def tearDown(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait()
        self.temp.cleanup()

    def write(self, path, value):
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(value))
        tmp.replace(path)

    def write_runtime(self, **changes):
        value = dict(pid=123, last_frame_system_ns=time.time_ns(), last_imu_system_ns=time.time_ns())
        value.update(changes)
        self.write(self.runtime, value)

    def wait_snapshot(self):
        time.sleep(.18)
        self.assertIsNone(self.process.poll())
        self.assertTrue((self.path / "left").exists())

    def health(self):
        script = FUNCTIONS + '''
HEALTH_HELPER_PID="$TEST_PID"
HEALTH_SNAPSHOT_DIR="$TEST_DIR"
LEFT_PID=123
runtime_status_path() { printf '%s' "$TEST_RUNTIME"; }
side_pid() { printf '%s' "$LEFT_PID"; }
read_side_health left "$TEST_CAL" || true
printf '%s %s %s' "$SIDE_CALIB_READY" "$SIDE_FRAME_FRESH" "$SIDE_IMU_FRESH"
'''
        env = dict(ENV, TEST_PID=str(self.process.pid), TEST_DIR=str(self.path),
                   TEST_RUNTIME=str(self.runtime), TEST_CAL=str(self.cal))
        return subprocess.check_output(['bash', '-eu', '-c', script], env=env, text=True)

    def test_healthy_and_stale(self):
        self.assertEqual(self.health(), 'true true true')
        self.write_runtime(last_frame_system_ns=time.time_ns()-5_000_000_000)
        self.wait_snapshot()
        self.assertEqual(self.health(), 'true false true')

    def test_invalid_and_missing(self):
        self.write_runtime(last_imu_system_ns={})
        self.wait_snapshot()
        self.assertEqual(self.health(), 'true true false')
        self.runtime.unlink()
        self.assertEqual(self.health(), 'false false false')

    def test_calibration_replacement(self):
        self.write(self.cal, {"valid": False})
        self.wait_snapshot()
        self.assertEqual(self.health(), 'false true true')
        self.write(self.cal, {"valid": True, "device_info": {"serial_number": "new"}})
        self.wait_snapshot()
        self.assertEqual(self.health(), 'true true true')

    def test_new_recorder_rejects_old_identity(self):
        self.write_runtime(pid=456)
        self.wait_snapshot()
        self.assertEqual(self.health(), 'false false false')

    def test_stalled_and_dead_helper(self):
        self.process.send_signal(signal.SIGSTOP)
        time.sleep(.65)
        self.assertEqual(self.health(), 'false false false')
        self.process.kill()
        self.process.wait()
        self.assertEqual(self.health(), 'false false false')

    def test_corrupt_snapshot(self):
        self.process.send_signal(signal.SIGSTOP)
        (self.path / 'left').write_text('broken 1 123 4 5\n')
        self.assertEqual(self.health(), 'false false false')
        (self.path / 'left').write_text('9999999999999999999999999 1 123 4 5\n')
        self.assertEqual(self.health(), 'false false false')

    def test_unknown_parser_does_not_restart_recorder(self):
        maintain = SOURCE[SOURCE.index('maintain_side_daemon() {'):SOURCE.index('maintain_side_daemon_by_name() {')]
        fifo = self.path / 'commands'
        os.mkfifo(fifo)
        script = maintain + """
check_side_device_paths() { return 0; }
check_side_working_state() { return 2; }
mark_session_error() { :; }
stop_side_daemon() { exit 98; }
maintain_side_daemon left ignored "$TEST_FIFO" video mcap ignored "$$"
"""
        subprocess.run(['bash', '-eu', '-c', script],
                       env=dict(ENV, TEST_FIFO=str(fifo)), check=True, timeout=2)

    def test_helper_recovery(self):
        script = FUNCTIONS + '''
SCRIPT_DIR="$TEST_SCRIPTS"
HEALTH_SNAPSHOT_DIR="$TEST_DIR"
LEFT_CALIB_JSON="$TEST_CAL"; RIGHT_CALIB_JSON="$TEST_CAL"
LEFT_RUNTIME_STATUS_JSON="$TEST_RUNTIME"; RIGHT_RUNTIME_STATUS_JSON="$TEST_RUNTIME"
FRAME_STALE_SEC=3; IMU_STALE_SEC=3
exec 9<>/dev/null
ensure_health_helper
first="$HEALTH_HELPER_PID"
sleep .2
kill -STOP "$first"
sleep 1.6
ensure_health_helper
[ "$first" != "$HEALTH_HELPER_PID" ]
sleep .2
kill -0 "$HEALTH_HELPER_PID"
kill "$HEALTH_HELPER_PID"
wait "$HEALTH_HELPER_PID" || true
'''
        # Use a separate directory from the fixture helper.
        other = self.path / 'recovery'; other.mkdir()
        env = dict(ENV, TEST_SCRIPTS=str(SCRIPTS), TEST_DIR=str(other),
                   TEST_CAL=str(self.cal), TEST_RUNTIME=str(self.runtime))
        subprocess.run(['bash', '-eu', '-c', script], env=env, check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
