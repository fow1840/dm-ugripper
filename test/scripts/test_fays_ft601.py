"""Exercise physical-port routing without selecting a live USB device."""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'standalone/FaysStereoRecorder/scripts/run_fays_stereo_daemon.sh').read_text()


def function(name):
    return re.search(r'^' + name + r'\(\) \{\n.*?^\}', SOURCE, re.M | re.S).group()


class PhysicalPortTest(unittest.TestCase):
    def route(self, config, key):
        with tempfile.NamedTemporaryFile(mode='w') as stream:
            stream.write(config)
            stream.flush()
            return subprocess.run(['bash', '-c', function('config_value') + '\n' +
                function('config_device_path') + '\nconfig_device_path "$1" "$2"',
                'test', stream.name, key], capture_output=True, text=True)

    def test_shared_transport(self):
        for key in ('stereo_dev_port', 'imu_dev_port'):
            result = self.route('device_type: ft601\nstereo_dev_port: 2-2.3.3\nimu_dev_port: NULL\n', key)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), '/sys/bus/usb/devices/2-2.3.3')

    def test_reject_nonphysical_selectors(self):
        for port in ('0', '/dev/video0', '../../8-1.4', '8-1.4:1.0', ''):
            result = self.route(f'device_type: ft601\nstereo_dev_port: {port}\n', 'stereo_dev_port')
            self.assertNotEqual(result.returncode, 0, port)
            self.assertEqual(result.stdout, '')

    def test_uvc_preserved(self):
        result = self.route('stereo_dev_port: /dev/left_fays_stereo\nimu_dev_port: /dev/left_fays_imu\n', 'imu_dev_port')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), '/dev/left_fays_imu')


if __name__ == '__main__':
    unittest.main()
