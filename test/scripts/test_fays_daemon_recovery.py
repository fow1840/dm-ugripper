"""Run the production Bash functions under errexit without camera hardware."""
import pathlib
import re
import subprocess
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / 'standalone/FaysStereoRecorder/scripts/run_fays_stereo_daemon.sh'
SOURCE = SCRIPT.read_text()


def function(name):
    return re.search(r'^' + name + r'\(\) \{\n.*?^\}', SOURCE, re.M | re.S).group()


class RecoveryTest(unittest.TestCase):
    def run_shell(self, body):
        result = subprocess.run(['bash', '-c', 'set -euo pipefail\n' + body], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_stale_fifo_recovers_under_errexit(self):
        output = self.run_shell(function('check_side_working_state_reason') + '\n' + function('maintain_side_daemon') + r'''
check_side_device_paths() { return 0; }
side_last_unhealthy_reason() { :; }
set_side_last_unhealthy_reason() { :; }
fays_log() { echo "$*"; }
mark_session_error() { :; }
stop_side_daemon() { echo stopped; rm -f "$2"; }
start_side_daemon() { echo started; }
RECORDING=false FINALIZE_PENDING=false
fifo=$(mktemp -u)
trap 'rm -f "$fifo"' EXIT
mkfifo "$fifo"
maintain_side_daemon left config "$fifo" video mcap calib ''
''')
        self.assertIn('reason=process not running', output)
        self.assertLess(output.index('stopped'), output.index('started'))

    def test_failed_worker_does_not_apply_partial_result(self):
        output = self.run_shell(function('maintain_stereo_daemons_parallel') + r'''
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
CONTROL_FIFO=$dir/control
fays_error() { echo "$*"; }
maintain_side_daemon_worker() {
    if [ "$1" = left ]; then
        echo partial > "$2"
        false
    fi
    echo complete > "$2"
}
apply_side_maintain_result() { echo "applied $1"; }
maintain_stereo_daemons_parallel
''')
        self.assertIn('left recorder maintain worker failed exit=1', output)
        self.assertNotIn('applied left', output)
        self.assertIn('applied right', output)

    def test_empty_result_preserves_pid(self):
        output = self.run_shell(function('apply_side_maintain_result') + r'''
result=$(mktemp)
trap 'rm -f "$result"' EXIT
fays_error() { echo "$*"; }
LEFT_PID=123
apply_side_maintain_result left "$result"
[ "$LEFT_PID" = 123 ]
''')
        self.assertIn('produced no result', output)


if __name__ == '__main__':
    unittest.main()
