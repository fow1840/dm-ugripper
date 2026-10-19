import contextlib
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import device_api as api
from common import Busy


class DeviceApiTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.firmware = self.root/'firmware.json'
        self.dpkg = self.root/'status'
        self.lock = self.root/'recording.lock'
        self.dpkg.write_text('Package: ugripper\nStatus: install ok installed\nArchitecture: arm64\nVersion: 2.1.15\n\n'
                             'Package: custom-app\nStatus: install ok installed\nVersion: 4.2\n\n'
                             'Package: removed\nStatus: deinstall ok config-files\nVersion: 1\n')
        for key, value in [('FIRMWARE_STATUS', self.firmware), ('DPKG_STATUS', self.dpkg),
                           ('RECORDING_LOCK', self.lock), ('STATE', self.root), ('_PACKAGE_CACHE', None)]:
            p = patch.object(api, key, value); p.start(); self.addCleanup(p.stop)
        p = patch.object(api, 'ego_status', return_value={'state':'unbound', 'bound':False, 'connected':None})
        p.start(); self.addCleanup(p.stop)

        p = patch.object(api, 'battery_status', return_value={'connected':True,'percent':50})
        p.start(); self.addCleanup(p.stop)

    def cache(self, **fields):
        self.firmware.write_text(json.dumps(dict(updated_at_ms=time.time()*1000, **fields)))

    def test_all_metadata_versions_and_arbitrary_packages(self):
        self.cache(device_versions={'hardware_version':'v2.5.0','data_version':'3.3',
                                    'cam_left_firmware_version':'iserial-label',
                                    'cam_chest_firmware_version':'disabled'})
        result = api.versions()['versions']
        self.assertEqual(result['software_version']['version'], 'v2.1.15')
        self.assertEqual(result['ugripper']['version'], '2.1.15')
        self.assertEqual(result['custom-app']['version'], '4.2')
        self.assertEqual(result['cam_chest_firmware_version']['status'], 'disabled')
        self.assertTrue(set(api.FIRMWARE_FIELDS).issubset(result))
        self.assertEqual(api.versions(['ugripper:arm64'])['versions']['ugripper:arm64']['version'], '2.1.15')
        self.assertEqual(api.versions(['removed'])['versions']['removed']['status'], 'not_installed')

    def test_package_database_refresh_and_missing_database(self):
        self.assertEqual(api.versions(['custom-app'])['versions']['custom-app']['version'], '4.2')
        replacement = self.dpkg.with_suffix('.new')
        replacement.write_text(self.dpkg.read_text().replace('4.2', '5.3'))
        replacement.replace(self.dpkg)
        self.assertEqual(api.versions(['custom-app'])['versions']['custom-app']['version'], '5.3')
        self.dpkg.unlink()
        self.assertEqual(api.versions(['custom-app'])['versions']['custom-app']['status'], 'unavailable')

    def test_cache_missing_malformed_stale_never_claims_current_versions(self):
        for value in ([], {}, {'updated_at_ms':1,'device_versions':{'hardware_version':'old'}},
                      {'updated_at_ms':True}, {'updated_at_ms':time.time()*1000,'device_versions':[]}):
            self.firmware.write_text(json.dumps(value))
            item = api.versions(['hardware_version'])['versions']['hardware_version']
            self.assertIsNone(item['version'])
            self.assertNotEqual(item['status'], 'ready')

    def test_fixed_inventory_unknown_disconnected_unrecognized_disabled(self):
        self.assertEqual(len(api.devices()['devices']), 16)
        self.assertIsNone(api.devices(['cam_left'])['devices']['cam_left']['connected'])
        self.cache(devices={
            'cam_left':{'enabled':True,'connected':False,'identified':False,'serial':''},
            'cam_right':{'enabled':True,'connected':True,'identified':False,'serial':''},
            'cam_chest':{'enabled':False,'connected':False},
            'left_encoder':{'enabled':True,'connected':True,'identified':None},
        })
        result = api.devices()['devices']
        self.assertEqual(result['cam_left']['status'], 'disconnected')
        self.assertEqual(result['cam_right']['status'], 'unrecognized')
        self.assertEqual(result['cam_chest']['status'], 'disabled')
        self.assertIsNone(result['left_encoder']['identified'])
        self.assertEqual(result['ego']['status'], 'unbound')
        self.firmware.write_text(json.dumps({'updated_at_ms':1,'devices':{'cam_left':{'connected':True,'serial':'old'}}}))
        item = api.devices(['cam_left'])['devices']['cam_left']
        self.assertEqual(item['status'], 'stale'); self.assertIsNone(item['connected']); self.assertIsNone(item['serial'])

    def test_recording_precheck_includes_battery_threshold(self):
        self.cache(recording={'active':False})
        with patch.object(api,'operation_lock',contextlib.nullcontext):
            for battery, allowed in [({'connected':True,'percent':29},False),
                                     ({'connected':True,'percent':30},True),
                                     ({'connected':True,'percent':31},True),
                                     ({'connected':False},False),({},False)]:
                with self.subTest(battery=battery),patch.object(api,'battery_status',return_value=battery):
                    value=api.recording()
                    self.assertEqual(value['can_upgrade'],allowed)
                    self.assertEqual(value['battery_ready'],allowed)

    def test_selection_is_explicit_and_validated(self):
        self.assertEqual(api.select({'fields':['ugripper,hardware_version','ugripper']},'fields'), ['ugripper','hardware_version'])
        for value in ('', '../x', '--help', 'a b'):
            with self.assertRaises(ValueError): api.select({'fields':[value]}, 'fields')
        with self.assertRaisesRegex(ValueError, 'unknown_device'): api.devices(['not_a_device'])
        self.assertEqual(set(api.versions(['custom-app'])['versions']), {'custom-app'})

    def test_recording_phases_and_unknown_never_allow_upgrade(self):
        with patch.object(api, 'operation_lock', contextlib.nullcontext):
            self.assertIsNone(api.recording()['active'])
            self.assertFalse(api.recording()['can_upgrade'])
            self.cache(recording={'active':False})
            self.assertTrue(api.recording()['can_upgrade'])
            for phase in ('starting', 'recording', 'finalizing'):
                self.lock.write_text(json.dumps({'pid':os.getpid(),'state':phase,'episode_dir':'episode-temp'}))
                result=api.recording()
                self.assertEqual(result['state'],phase); self.assertTrue(result['active']); self.assertFalse(result['can_upgrade'])
            self.lock.write_text('{bad')
            self.assertEqual(api.recording()['state'],'unknown'); self.assertFalse(api.recording()['can_upgrade'])
            self.lock.unlink()
            with patch.object(api,'operation_lock',side_effect=Busy('occupied')):
                self.assertFalse(api.recording()['can_upgrade'])
            (self.root/'task.json').write_text(json.dumps({'reboot_pending':True}))
            self.assertFalse(api.recording()['can_upgrade'])


if __name__ == '__main__': unittest.main()
