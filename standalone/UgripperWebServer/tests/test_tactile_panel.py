import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import tactile_panel as panel
from ego_status import ego_status

class PanelTest(unittest.TestCase):
    def test_sensor_labels_are_unambiguous(self):
        self.assertEqual(len(set(c['label'] for c in panel.CAMERAS.values())),4)
        self.assertEqual(panel.CAMERAS['right_tcam_l']['label'],'右夹爪 · 左触觉传感器')

    def test_warning_follows_matching_serial_and_both_histories(self):
        with patch.object(panel,'sensor_serial',return_value='new'), patch.object(panel,'read_json',return_value={'cameras':{'old':{'persistent_fault_active':True},'new':{'recent':[True,True,False]}}}):
            self.assertFalse(panel.panel_status()[0]['warning'])
        with patch.object(panel,'sensor_serial',return_value='new'), patch.object(panel,'read_json',return_value={'cameras':{'new':{'persistent_fault_active':True}}}):
            self.assertTrue(panel.panel_status()[0]['warning'])

    def test_reset_rejects_changed_sensor_before_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp)
            (out/'result.json').write_text(json.dumps({'cameras':[dict(camera='left_tcam_l',serial='old',status='ok',captured_at_ms=time.time()*1000)]}))
            with patch.object(panel,'sensor_serial',return_value='new'):
                with self.assertRaisesRegex(ValueError,'更换'):
                    panel.reset_baseline(out,'left_tcam_l','both','old')
            self.assertFalse((out/'web_reset_request.json').exists())

    def test_reset_rejects_stale_preview(self):
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp)
            (out/'result.json').write_text(json.dumps({'cameras':[dict(camera='left_tcam_l',serial='same',status='ok',captured_at_ms=1)]}))
            with self.assertRaisesRegex(ValueError,'两分钟'):
                panel.reset_baseline(out,'left_tcam_l','reference','same')

    def test_versions_remain_cached_while_disconnected_and_clear_on_unbind(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            (root/'ego_binding.json').write_text(json.dumps(dict(serial='ego',app={'version_name':'1.0.5'},firmware={'version':'build-1'})))
            (root/'ego_binding_status.json').write_text(json.dumps(dict(serial='ego',status='disconnected',updated_at_ms=100)))
            value=ego_status(root,100)
            self.assertFalse(value['connected'])
            self.assertEqual(value['firmware_version'],'build-1')
            self.assertEqual(value['software_version'],'1.0.5')
            (root/'ego_binding.json').unlink()
            self.assertNotIn('firmware_version',ego_status(root,100))

if __name__=='__main__':unittest.main()
