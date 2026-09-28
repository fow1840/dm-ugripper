import hashlib
import struct
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import boot_firmware as boot
from common import atomic_json, read_json


def fdt(nodes):
    names = bytearray(); offsets = {}
    structure = bytearray()
    def word(value): structure.extend(struct.pack('>I', value))
    def aligned(value):
        structure.extend(value)
        while len(structure) % 4: structure.append(0)
    def node(name, props, children):
        word(1); aligned(name.encode()+b'\0')
        for key, value in props.items():
            if key not in offsets:
                offsets[key] = len(names); names.extend(key.encode()+b'\0')
            word(3); word(len(value)); word(offsets[key]); aligned(value)
        for child in children: node(*child)
        word(2)
    node(*nodes);word(9)
    offset=56;strings=offset+len(structure);total=strings+len(names)
    return struct.pack('>10I',0xd00dfeed,total,offset,strings,40,17,16,0,len(names),len(structure))+bytes(16)+structure+names


def image():
    dt=fdt(('',{'compatible':b'vendor,board\0rockchip,rk3588\0'},[]))
    def part(name,kind,data):
        return (name,{'arch':b'arm64\0','type':kind,'data':data},[
            ('hash',{'algo':b'sha256\0','value':hashlib.sha256(data).digest()},[])])
    return fdt(('',{},[
        ('images',{},[part('kernel',b'kernel\0',b'kernel bytes'),part('fdt',b'flat_dt\0',dt)]),
        ('configurations',{'default':b'conf\0'},[('conf',{'kernel':b'kernel\0','fdt':b'fdt\0'},[])])]))


class BootFirmwareTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.path=self.root/'boot.img';self.path.write_bytes(image())
        self.bootid=self.root/'boot_id';self.bootid.write_text('before')
        self.marker=self.root/'pending.json'
        self.bms=self.root/'bms.txt';self.bms.write_text(f'timestamp: {int(time.time())}\nsoc_percent: 50\n')
        for name,value in [('PENDING',self.marker),('BOOT_ID',self.bootid),('STATE',self.root),('BMS_SOURCE',self.bms)]:
            patcher=patch.object(boot,name,value);patcher.start();self.addCleanup(patcher.stop)

    def test_battery_threshold_and_charging_does_not_bypass(self):
        with patch.object(boot.time,'time',return_value=100):
            for percent in (0,19,20,21,100):
                self.bms.write_text(f'timestamp: 100\nsoc_percent: {percent}\nisCharging: 1\n')
                if percent < 20:
                    with self.assertRaisesRegex(ValueError,'低于 20%'):boot.require_battery()
                else:self.assertEqual(boot.require_battery(),percent)

    def test_unknown_stale_or_malformed_battery_blocks(self):
        samples=['', 'timestamp: 100\n', 'timestamp: 79\nsoc_percent: 90',
                 'timestamp: 101\nsoc_percent: 90','timestamp: 100\nsoc_percent: 101',
                 'timestamp: 100\nsoc_percent: 20.5','timestamp: 100\nsoc_percent: -1',
                 'timestamp: 100\nsoc_percent: 50\nsoc_percent: 90']
        with patch.object(boot.time,'time',return_value=100):
            for data in samples:
                self.bms.write_text(data)
                with self.subTest(data=data),self.assertRaisesRegex(ValueError,'不可用'):boot.require_battery()
            self.bms.unlink()
            with self.assertRaises(ValueError):boot.require_battery()

    def test_low_battery_rejects_submission_before_side_effects(self):
        import maintenance
        self.bms.write_text(f'timestamp: {int(time.time())}\nsoc_percent: 19\n')
        with patch.object(maintenance,'MUTEX') as mutex,patch.object(maintenance,'operation_lock') as lock:
            with self.assertRaisesRegex(ValueError,'低于 20%'):
                maintenance.submit({'kind':'rk3588_boot','confirm':True,'upload':'a'*32})
            mutex.acquire.assert_not_called();lock.assert_not_called()

    def test_battery_drop_during_inspection_blocks_before_write(self):
        from unittest.mock import Mock
        run=Mock(return_value='')
        with patch.object(boot,'require_battery',side_effect=[20,ValueError('low battery')]),patch.object(boot,'check_target'),patch.object(boot,'partition_hash',return_value='old'),patch.object(boot,'persist') as persist:
            with self.assertRaisesRegex(ValueError,'low battery'):boot.install(self.path,{'id':'test'},Mock(),run)
            run.assert_not_called();persist.assert_not_called()

    def test_valid_fit_and_corrupt_payload(self):
        result=boot.inspect_image(self.path)
        self.assertEqual(result['sha256'],hashlib.sha256(self.path.read_bytes()).hexdigest())
        self.path.write_bytes(self.path.read_bytes().replace(b'kernel bytes',b'broken bytes'))
        with self.assertRaisesRegex(ValueError,'SHA-256'):boot.inspect_image(self.path)

    def test_reject_wrong_arch_and_truncated_and_camera_image(self):
        for data in [b'camera image', image()[:100], image().replace(b'arm64\0',b'arm32\0')]:
            self.path.write_bytes(data)
            with self.assertRaises(ValueError):boot.inspect_image(self.path)

    def test_same_image_does_not_write_or_reboot(self):
        digest=boot.inspect_image(self.path)['sha256']
        with patch.object(boot,'check_target'),patch.object(boot,'partition_hash',return_value=digest),patch.object(boot,'persist') as persist:
            from unittest.mock import Mock
            run=Mock();result=boot.install(self.path,{'id':'test'},Mock(),run)
            run.assert_not_called();persist.assert_not_called();self.assertIn('跳过',result['status'])

    def test_write_verify_reboot_and_recover(self):
        digest=boot.inspect_image(self.path)['sha256'];calls=[]
        def run(args,*rest):
            calls.append(args)
            if args[:2]==['systemctl','reboot']:raise SystemExit('simulated shutdown')
            return ''
        with patch.object(boot,'check_target'),patch.object(boot,'partition_hash',side_effect=['old',digest]):
            with self.assertRaises(SystemExit):boot.install(self.path,{'id':'test'},lambda *a,**k:None,run)
        self.assertEqual(calls[0],['dd','if='+str(self.path),'of=/dev/mmcblk0p3','bs=4M','conv=fsync','status=progress'])
        self.assertEqual(read_json(self.marker)['stage'],'awaiting_reboot')
        atomic_json(self.root/'task.json',{'id':'test','status':'running'})
        self.bootid.write_text('after')
        with patch.object(boot,'check_target'),patch.object(boot,'partition_hash',return_value=digest):boot.recover(run)
        self.assertEqual(read_json(self.root/'task.json')['status'],'success')
        self.assertFalse(self.marker.exists())

    def test_bad_readback_never_reboots(self):
        from unittest.mock import Mock
        run=Mock(return_value='')
        with patch.object(boot,'check_target'),patch.object(boot,'partition_hash',return_value='mismatch'):
            with self.assertRaisesRegex(RuntimeError,'SHA-256'):boot.install(self.path,{'id':'test'},Mock(),run)
        self.assertFalse(any(c.args[0][0]=='systemctl' for c in run.call_args_list))
        self.assertEqual(read_json(self.marker)['status'],'failed')

    def test_service_restart_without_reboot_never_reports_success(self):
        atomic_json(self.root/'task.json',{'id':'test','status':'running'})
        atomic_json(self.marker,{'id':'test','status':'running','stage':'awaiting_reboot','boot_id':'before'})
        with patch.object(boot,'partition_hash') as read:boot.recover(lambda *a:None)
        read.assert_not_called();self.assertEqual(read_json(self.root/'task.json')['status'],'failed')

    def test_external_payload_hash_and_bounds(self):
        dt=fdt(('',{'compatible':b'rockchip,rk3588\0'},[]))
        kernel=b'kernel';offset=4096
        def part(name,kind,data,start):
            return (name,{'arch':b'arm64\0','type':kind,
                         'data-size':struct.pack('>I',len(data)),
                         'data-position':struct.pack('>I',start)},[
                ('hash',{'algo':b'sha256\0','value':hashlib.sha256(data).digest()},[])])
        header=fdt(('',{},[
            ('images',{},[part('kernel',b'kernel\0',kernel,offset),part('fdt',b'flat_dt\0',dt,offset+len(kernel))]),
            ('configurations',{'default':b'conf\0'},[('conf',{'kernel':b'kernel\0','fdt':b'fdt\0'},[])])]))
        content=header.ljust(offset,b'\0')+kernel+dt
        self.path.write_bytes(content);boot.inspect_image(self.path)
        self.path.write_bytes(content[:-1])
        with self.assertRaisesRegex(ValueError,'范围'):boot.inspect_image(self.path)

    def test_reboot_verification_mismatch_is_failure(self):
        atomic_json(self.root/'task.json',{'id':'test','status':'running'})
        atomic_json(self.marker,{'id':'test','status':'running','stage':'awaiting_reboot',
                                'boot_id':'old','size':100,'sha256':'expected'})
        with patch.object(boot,'check_target'),patch.object(boot,'partition_hash',return_value='bad'):
            boot.recover(lambda *a:None)
        self.assertEqual(read_json(self.root/'task.json')['status'],'failed')

    def test_reboot_timeout_marks_failure(self):
        from unittest.mock import Mock
        digest=boot.inspect_image(self.path)['sha256']
        with patch.object(boot,'check_target'),patch.object(boot,'partition_hash',side_effect=['old',digest]),patch.object(boot.time,'sleep'):
            with self.assertRaisesRegex(RuntimeError,'重启未完成'):
                boot.install(self.path,{'id':'test'},Mock(),Mock(return_value=''))
        self.assertEqual(read_json(self.marker)['status'],'failed')

    def test_wrong_partition_name_rejected(self):
        compatible=self.root/'compatible';compatible.write_bytes(b'rockchip,rk3588\0')
        uevent=self.root/'uevent';uevent.write_text('PARTNAME=rootfs\nPARTN=3\n')
        with patch.object(boot,'COMPATIBLE',compatible),patch.object(boot,'UEVENT',uevent),patch.object(boot,'DEVICE',self.path),patch.object(boot.stat,'S_ISBLK',return_value=True):
            with self.assertRaisesRegex(ValueError,'boot 分区'):boot.check_target(10,lambda *a:'1000')

if __name__=='__main__':unittest.main()
