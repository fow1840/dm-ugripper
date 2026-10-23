import hashlib
import io
import tarfile
import struct
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

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


def release(path, changes=None):
    files = {'update.sh': b'#!/bin/sh\nexit 0\n',
             'payload/version.info': b'V1.0.3.2_20261022143700_release\n',
             'payload/boot-v2.5.img': image(), 'payload/boot-v3.img': image(),
             'payload/fstab': b'fstab'}
    files.update({name: name.encode() for name in boot.INSTALLED_FILES})
    files.update(changes or {})
    files['SHA256SUMS'] = ''.join(hashlib.sha256(value).hexdigest()+'  '+name+'\n'
                                 for name,value in files.items()).encode()
    with tarfile.open(path,'w:gz') as archive:
        for name,content in files.items():
            member=tarfile.TarInfo('release/'+name); member.size=len(content)
            archive.addfile(member,io.BytesIO(content))


class BootFirmwareTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.path=self.root/'firmware.tar.gz';release(self.path)
        self.bootid=self.root/'boot_id';self.bootid.write_text('before')
        self.marker=self.root/'pending.json'
        self.compatible=self.root/'compatible';self.compatible.write_bytes(b'vendor,V100\0rockchip,rk3588\0')
        self.bms=self.root/'bms.txt';self.bms.write_text(f'timestamp: {int(time.time())}\nsoc_percent: 50\n')
        self.version=self.root/'version.info'
        for name,value in [('PENDING',self.marker),('BOOT_ID',self.bootid),('STATE',self.root),
                           ('COMPATIBLE',self.compatible),('BMS_SOURCE',self.bms),('VERSION_INFO',self.version)]:
            patcher=patch.object(boot,name,value);patcher.start();self.addCleanup(patcher.stop)

    def test_battery_threshold_and_charging_does_not_bypass(self):
        with patch.object(boot.time,'time',return_value=100):
            for percent in (0,19,20,29,30,31,100):
                self.bms.write_text(f'timestamp: 100\nsoc_percent: {percent}\nisCharging: 1\n')
                if percent < 30:
                    with self.assertRaisesRegex(ValueError,'低于 30%'):boot.require_battery()
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
        self.bms.write_text(f'timestamp: {int(time.time())}\nsoc_percent: 29\n')
        for kind in ('software','main_camera','gripper','rk3588_boot'):
            for source in ({'upload':'a'*32}, {'external':True,'package':'firmware.tar.gz','force':True}):
                with self.subTest(kind=kind),patch.object(maintenance,'MUTEX') as mutex,patch.object(maintenance,'operation_lock') as lock:
                    with self.assertRaisesRegex(ValueError,'低于 30%'):maintenance.submit(dict(kind=kind,**source))
                    mutex.acquire.assert_not_called();lock.assert_not_called()

    def test_battery_drop_during_inspection_blocks_before_script(self):
        run=Mock(return_value='')
        with patch.object(boot,'require_battery',side_effect=[30,ValueError('low battery')]),patch.object(boot,'check_target'),patch.object(boot,'persist') as persist:
            with self.assertRaisesRegex(ValueError,'low battery'):boot.install(self.path,{'id':'test'},Mock(),run)
            run.assert_not_called();persist.assert_not_called()
        self.assertFalse(list(self.root.glob('firmware-*')))

    def test_package_checksum_and_hardware_selection(self):
        info=boot.inspect_package(self.path)
        self.assertEqual(info['firmware_version'],'V1.0.3.2_20261022143700_release')
        self.assertEqual(info['hardware_model'],'V100')
        self.compatible.write_bytes(b'vendor,V130\0rockchip,rk3588\0')
        self.assertEqual(boot.inspect_package(self.path)['hardware_model'],'V130')
        self.compatible.write_bytes(b'rockchip,rk3588\0')
        with self.assertRaisesRegex(ValueError,'硬件型号'):boot.inspect_package(self.path)

    def test_raw_image_corrupt_manifest_and_missing_version_rejected(self):
        self.path.write_bytes(image())
        with self.assertRaisesRegex(ValueError,'完整升级包'):boot.inspect_package(self.path)
        for changes in ({'payload/version.info':b''},{'extra.sh':b'unverified'}):
            release(self.path,changes)
            if 'extra.sh' in changes:
                # Checked extra payloads are allowed; an unlisted mutation is not.
                with boot.unpack_package(self.path) as root:
                    (root/'update.sh').write_text('tampered')
                    with self.assertRaisesRegex(ValueError,'SHA-256'):boot.inspect_package_root(root)
            else:
                with self.assertRaisesRegex(ValueError,'version.info'):boot.inspect_package(self.path)

    def test_archive_traversal_links_and_duplicate_names_rejected(self):
        for name,kind in [('../escape','file'),('/absolute','file'),('release/link','link'),('release/update.sh','duplicate')]:
            with tarfile.open(self.path,'w') as archive:
                member=tarfile.TarInfo(name)
                if kind=='link': member.type=tarfile.SYMTYPE;member.linkname='/etc'
                archive.addfile(member)
                if kind=='duplicate':archive.addfile(member)
            with self.subTest(name=name),self.assertRaises(ValueError):boot.inspect_package(self.path)
        self.assertFalse((self.root.parent/'escape').exists())

    def test_script_execution_verify_and_pending_reboot_even_same_version(self):
        run=Mock(return_value='')
        with patch.object(boot,'check_target'),patch.object(boot,'verify_installed') as verify:
            result=boot.install(self.path,{'id':'test'},Mock(),run)
        self.assertEqual(result['status'],'awaiting_reboot');self.assertFalse(result['boot_verified'])
        command=run.call_args_list[0].args[0]
        self.assertEqual(command[:2],['/bin/sh','-e']);self.assertEqual(command[2].name,'update.sh')
        self.assertEqual(run.call_args_list[0].args[1],900)
        self.assertEqual(read_json(self.marker)['firmware_version'],result['firmware_version'])
        verify.assert_called_once();self.assertFalse(command[2].exists())
        self.assertFalse(any(c.args[0][0] in ('systemctl','dd') for c in run.call_args_list))

    def test_script_failure_and_bad_readback_never_report_success(self):
        for script_error in (True,False):
            run=Mock(side_effect=RuntimeError('script failed') if script_error else None,return_value='')
            with patch.object(boot,'check_target'),patch.object(boot,'verify_installed',side_effect=RuntimeError('bad readback')):
                with self.assertRaises(RuntimeError):boot.install(self.path,{'id':'test'},Mock(),run)
            self.assertEqual(read_json(self.marker)['status'],'failed')
            self.assertFalse(list(self.root.glob('firmware-*')))

    def test_verifies_version_files_and_boot_partition(self):
        files={name:self.root/str(i) for i,name in enumerate(boot.INSTALLED_FILES)}
        for file in files.values():file.write_bytes(b'installed')
        self.version.write_text('V1.0.3.2\n')
        info=dict(size=100,sha256='digest',firmware_version='V1.0.3.2',files={n:boot.file_hash(p) for n,p in files.items()})
        with patch.object(boot,'INSTALLED_FILES',files),patch.object(boot,'partition_hash',return_value='digest'):
            boot.verify_installed(info)
            self.version.write_text('wrong')
            with self.assertRaisesRegex(RuntimeError,'version.info'):boot.verify_installed(info)
            self.version.write_text('V1.0.3.2')
            next(iter(files.values())).write_bytes(b'corrupt')
            with self.assertRaisesRegex(RuntimeError,'文件 SHA-256'):boot.verify_installed(info)

    def test_read_version_rejects_missing_malformed_and_oversized_content(self):
        self.assertEqual(boot.read_version(),'')
        for value in ('', 'a\nb', 'v'*257, 'bad version', '-invalid', 'V1.00' + ' '*4092):
            self.version.write_text(value);self.assertEqual(boot.read_version(),'')
        self.version.write_text('V1.0.3.2_20261022143700_release\n')
        self.assertEqual(boot.read_version(),'V1.0.3.2_20261022143700_release')

    def test_service_restart_preserves_waiting_and_new_boot_verifies(self):
        info=boot.inspect_package(self.path)
        atomic_json(self.root/'task.json',{'id':'test','status':'running'})
        atomic_json(self.marker,dict(id='test',status='running',stage='awaiting_reboot',boot_id='before',**info))
        with patch.object(boot,'verify_installed') as verify:boot.recover(Mock())
        verify.assert_not_called();self.assertEqual(read_json(self.root/'task.json')['status'],'awaiting_reboot')
        self.bootid.write_text('after')
        with patch.object(boot,'check_target'),patch.object(boot,'verify_installed') as verify:boot.recover(Mock())
        verify.assert_called_once();self.assertTrue(read_json(self.root/'task.json')['boot_verified'])
        self.assertFalse(self.marker.exists())

    def test_reboot_verification_mismatch_is_failure(self):
        atomic_json(self.root/'task.json',{'id':'test','status':'running'})
        atomic_json(self.marker,{'id':'test','status':'running','stage':'awaiting_reboot','boot_id':'old','size':100})
        with patch.object(boot,'check_target'),patch.object(boot,'verify_installed',side_effect=RuntimeError('bad')):boot.recover(Mock())
        self.assertEqual(read_json(self.root/'task.json')['status'],'failed')

    def test_valid_fit_and_corrupt_payload(self):
        path=self.root/'boot.img';path.write_bytes(image())
        self.assertEqual(boot.inspect_image(path)['sha256'],hashlib.sha256(path.read_bytes()).hexdigest())
        path.write_bytes(image().replace(b'kernel bytes',b'broken bytes'))
        with self.assertRaisesRegex(ValueError,'SHA-256'):boot.inspect_image(path)


if __name__=='__main__':unittest.main()
