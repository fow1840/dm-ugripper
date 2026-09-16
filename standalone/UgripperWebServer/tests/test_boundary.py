import contextlib
import importlib
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import common
import maintenance
import server

class BoundaryTests(unittest.TestCase):
    def test_mutual_exclusion_survives_same_process_separate_opens(self):
        with tempfile.TemporaryDirectory() as d, patch.object(common,'LOCK',Path(d)/'lock'), patch.object(common,'PRODUCTION_LOCK',Path(d)/'production.lock'):
            common.PRODUCTION_LOCK.touch()
            with common.operation_lock():
                with self.assertRaises(common.Busy):
                    with common.operation_lock():pass
            with common.operation_lock():pass
    def test_download_traversal_and_symlinks_rejected(self):
        with tempfile.TemporaryDirectory() as d, patch.object(server,'DATA',Path(d)), patch('server.os.path.ismount',return_value=True):
            (Path(d)/'secret').symlink_to('/etc/passwd')
            for name in ('../etc/passwd','secret','new-temp','a/../b','/etc/passwd'):
                with self.assertRaises((ValueError,OSError)):server.open_data(name)
            (Path(d)/'file').write_text('data')
            fd=server.open_data('file')
            self.assertEqual(os.read(fd,4),b'data');os.close(fd)
    def test_unmounted_data_root_is_not_exposed(self):
        with patch('server.os.path.ismount',return_value=False):
            with self.assertRaises(ValueError):server.open_data('')
    def test_hex_checksums_before_vendor_loader(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'x.hex';p.write_text(':0100000001FE\n:00000001FF\n')
            maintenance.validate_hex(p)
            p.write_text(':0100000001FF\n:00000001FF\n')
            with self.assertRaises(ValueError):maintenance.validate_hex(p)
    def test_zero_requires_fixture_and_unknown_jobs_rejected(self):
        for req in ({'kind':'zeroing'},{'kind':'shell'}):
            with self.assertRaises(ValueError):maintenance.submit(req)
    def test_upload_identifiers_cannot_escape(self):
        import builtins
        with tempfile.TemporaryDirectory() as d, patch.object(common,'LOCK',Path(d)/'lock'), patch.object(common,'PRODUCTION_LOCK',Path(d)/'production.lock'):
            common.PRODUCTION_LOCK.touch()
            with patch('maintenance.open',side_effect=lambda *a:builtins.open(Path(d)/'usb.lock','a')):
                for value in ('../x','a'*33,'-option'):
                    with self.assertRaises(ValueError):
                        maintenance.submit({'kind':'gripper','upload':value})
                    self.assertTrue(maintenance.MUTEX.acquire(False))
                    maintenance.MUTEX.release()
                    with common.operation_lock():pass
    def test_process_timeout_is_bounded(self):
        import subprocess
        with self.assertRaises(subprocess.TimeoutExpired):
            common.command(['/bin/sleep','5'],timeout=0.05)
    def test_dfu_command_is_pinned(self):
        cmd=maintenance.dfu.build_dfu_command('/opt/ugripper/bin/UgripperWebServer/bin/dfu-util','9-1.2.1','/private/image.img')
        self.assertEqual(cmd[1:7],['-d','0955:701a','-p','9-1.2.1','-a','0'])
        with self.assertRaises(maintenance.dfu.DfuError):maintenance.dfu.build_dfu_command('dfu','--help','f')

if __name__=='__main__':unittest.main()
