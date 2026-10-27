import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
def module(name, path):
    spec=importlib.util.spec_from_file_location(name, ROOT/path)
    obj=importlib.util.module_from_spec(spec); spec.loader.exec_module(obj); return obj
backend=module('backend','standalone/FaysStereoRecorder/scripts/select_fays_backend.py')
memory=module('memory','config/ft601_usb_memory.py')
status=module('status','standalone/UgripperWebServer/fays_status.py')

class Integration(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.dev=self.root/'bus/usb/devices/2-2.3.3';self.dev.mkdir(parents=True)
        (self.dev/'idVendor').write_text('0403');(self.dev/'idProduct').write_text('601f')
        self.quota=self.root/'module/usbcore/parameters/usbfs_memory_mb';self.quota.parent.mkdir(parents=True);self.quota.write_text('16')
        self.state=self.root/'state.json'
    def test_physical_selection(self):
        kind,text=backend.select('stereo_dev_port: 2-2.3.3\nimu_dev_port: old\n',self.root)
        self.assertEqual(kind,'ft601');self.assertIn('imu_dev_port: NULL',text)
        self.assertIn('stereo_dev_port: 2-2.3.3',text)
        (self.dev/'idProduct').write_text('602e')
        original='stereo_dev_port: 2-2.3.3\nimu_dev_port: old\n'
        self.assertEqual(backend.select(original,self.root),('uvc',original))
    def test_unknown_or_missing_rejected(self):
        (self.dev/'idProduct').write_text('0000')
        with self.assertRaises(ValueError): backend.select('stereo_dev_port: 2-2.3.3',self.root)
        with self.assertRaises(ValueError): backend.select('stereo_dev_port: 2-2.3.4',self.root)
    def test_quota_restore_and_external_change(self):
        memory.apply(self.root,self.state);self.assertEqual(int(self.quota.read_text()),64)
        memory.apply(self.root,self.state);memory.restore(self.root,self.state)
        self.assertEqual(int(self.quota.read_text()),16)
        memory.apply(self.root,self.state);self.quota.write_text('128');memory.restore(self.root,self.state)
        self.assertEqual(int(self.quota.read_text()),128)
    def test_no_camera_and_unlimited_untouched(self):
        self.quota.write_text('0');memory.apply(self.root,self.state)
        self.assertFalse(self.state.exists());self.assertEqual(self.quota.read_text(),'0')
        self.quota.write_text('16');(self.dev/'idProduct').write_text('602e');memory.apply(self.root,self.state)
        self.assertFalse(self.state.exists());self.assertEqual(self.quota.read_text(),'16')
    def test_status_requires_live_process_fresh_data(self):
        (self.root/'left_fays_stereo').touch()
        file=self.root/'umi_left_fays_runtime_status.json'
        data=dict(pid=os.getpid(),running=True,last_frame_system_ns=10**10,status_write_system_ns=10**10)
        file.write_text(json.dumps(data))
        check=lambda:status.stream_status('left',devroot=self.root,shm=self.root,now_ns=11*10**9)
        self.assertEqual(check(),dict(present=True,stream=True))
        data['last_frame_system_ns']=1;file.write_text(json.dumps(data));self.assertFalse(check()['stream'])
        data['last_frame_system_ns']=10**10;file.write_text(json.dumps(data))
        with patch.object(status.os,'kill',side_effect=ProcessLookupError):self.assertFalse(check()['stream'])
        (self.root/'left_fays_stereo').unlink();self.assertFalse(check()['present'])
if __name__=='__main__':unittest.main()
