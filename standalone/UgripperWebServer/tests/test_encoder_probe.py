import contextlib
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import encoder_probe as probe
from common import Busy


class EncoderProbeTests(unittest.TestCase):
    def test_signed_angles_preserve_raw_count(self):
        for raw, signed in ((0, 0), (1, 1), (32767, 32767),
                            (32768, -32768), (61364, -4172), (65535, -1)):
            with self.subTest(raw=raw):
                sample = probe.position_sample(raw)
                self.assertEqual(sample['position'], raw)
                self.assertAlmostEqual(sample['angle_rad'], signed * math.tau / 65536)
                self.assertAlmostEqual(sample['angle_deg'], signed * 360 / 65536)
        self.assertAlmostEqual(probe.position_sample(61364)['angle_rad'], -0.4, places=4)

    def test_fragmented_position_after_noise_and_bad_crc(self):
        # Known Modbus response for 0x1234: 01 03 02 12 34 b5 33.
        chunks = [b'noise\x01\x03\x02\x12\x34\x00\x00\x01', b'\x03\x02\x12', b'\x34\xb5\x33']
        with patch.object(probe.termios, 'tcflush'), patch.object(probe.os, 'write', return_value=8) as write, patch.object(probe.select, 'select', return_value=([10], [], [])), patch.object(probe.os, 'read', side_effect=chunks):
            self.assertEqual(probe.read_position(10), 0x1234)
            write.assert_called_once_with(10, bytes.fromhex('01 03 00 41 00 01 d4 1e'))

    def test_missing_or_corrupt_response_never_becomes_zero(self):
        with patch.object(probe.termios, 'tcflush'), patch.object(probe.os, 'write', return_value=8), patch.object(probe.select, 'select', return_value=([], [], [])):
            self.assertIsNone(probe.read_position(10))

    def test_busy_never_opens_serial(self):
        with patch.object(probe, 'operation_lock', side_effect=Busy('recording')), patch.object(probe.os, 'open') as opened:
            with self.assertRaises(Busy):
                probe.read_encoders()
            opened.assert_not_called()

    def test_restores_tty_and_closes_on_error(self):
        attrs = [0, 0, 0, 0, 0, 0, [0] * 32]
        with patch.object(probe.os, 'open', return_value=10), patch.object(probe.os, 'close') as close, patch.object(probe.termios, 'tcgetattr', side_effect=lambda _: [*attrs[:6], list(attrs[6])]), patch.object(probe.termios, 'tcsetattr') as configure, patch.object(probe, 'read_position', side_effect=OSError('unplugged')):
            self.assertEqual(probe.read_encoder('/dev/left_encoder')['status'], 'unavailable')
            close.assert_called_once_with(10)
            self.assertEqual(configure.call_args.args[2], attrs)

    def test_lock_covers_both_sides_and_independent_failure(self):
        held = []
        @contextlib.contextmanager
        def lock():
            held.append(True)
            try: yield
            finally: held.pop()
        def read(path):
            self.assertTrue(held)
            return {'status': 'ok' if str(path).endswith('left_encoder') else 'unavailable'}
        with patch.object(probe, 'operation_lock', lock), patch.object(probe, 'read_encoder', side_effect=read):
            result = probe.read_encoders()
        self.assertEqual(result['left']['status'], 'ok')
        self.assertEqual(result['right']['status'], 'unavailable')
        self.assertFalse(held)


if __name__ == '__main__':
    unittest.main()
