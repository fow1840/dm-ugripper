import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tactile_panel as panel

class AlarmTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.patch = patch.object(panel, 'TACTILE_STATE', self.root)
        self.patch.start()
        self.addCleanup(self.tmp.cleanup)
        self.addCleanup(self.patch.stop)

    def save(self, generation='first', camera='left_tcam_l'):
        value = dict(id=generation, episode='episode_test', updated_at_ms=123,
                     width=160, height=120, images=[dict(camera=camera, serial='test-sn',
                     scope='reference', mask='1'+'0'*19199, current_gray_hex='64'*19200, kind='solid_core')])
        (self.root/'latest_alarm.json').write_text(json.dumps(value))
        return value

    def test_missing_and_corrupt_evidence(self):
        self.assertEqual(panel.latest_alarm(), {})
        (self.root/'latest_alarm.json').write_text('broken')
        self.assertIn('error', panel.latest_alarm())

    def test_summary_and_exact_png_pixels(self):
        self.save()
        self.assertNotIn('mask', panel.latest_alarm()['images'][0])
        self.assertNotIn('current_gray_hex', panel.latest_alarm()['images'][0])
        png = panel.alarm_mask_png('first', 'left_tcam_l', 'reference')
        self.assertEqual(png[:8], b'\x89PNG\r\n\x1a\n')
        pos=8; compressed=b''
        while pos < len(png):
            size=struct.unpack('>I',png[pos:pos+4])[0];kind=png[pos+4:pos+8]
            data=png[pos+8:pos+8+size]
            self.assertEqual(struct.unpack('>I',png[pos+8+size:pos+12+size])[0], zlib.crc32(kind+data)&0xffffffff)
            if kind==b'IDAT': compressed+=data
            pos+=12+size
        raw=zlib.decompress(compressed)
        self.assertEqual(len(raw), 481*120)
        self.assertEqual(raw[:7], bytes((0,185,45,45,100,100,100)))
        self.assertEqual(raw[481:485], bytes((0,100,100,100)))

    def test_rotation_rejects_old_generation_and_old_sensor(self):
        self.save()
        self.save('second','right_tcam_r')
        self.assertEqual(len(list(self.root.iterdir())),1)
        self.assertEqual(panel.latest_alarm()['id'],'second')
        with self.assertRaises(ValueError):panel.alarm_mask_png('first','left_tcam_l','reference')
        with self.assertRaises(ValueError):panel.alarm_mask_png('second','left_tcam_l','reference')
        self.assertTrue(panel.alarm_mask_png('second','right_tcam_r','reference').startswith(b'\x89PNG'))

    def test_missing_or_invalid_cover_never_falls_back_to_mask_only(self):
        for frame in (None, '', 'xx'*19200):
            value=self.save();value['images'][0]['current_gray_hex']=frame
            (self.root/'latest_alarm.json').write_text(json.dumps(value))
            self.assertIn('error',panel.latest_alarm())
            with self.assertRaises(ValueError):panel.alarm_mask_png('first','left_tcam_l','reference')

    def test_invalid_mask_and_path_rejected(self):
        value=self.save();value['images'][0]['mask']='x'*19200
        (self.root/'latest_alarm.json').write_text(json.dumps(value))
        self.assertIn('error',panel.latest_alarm())
        self.save()
        with self.assertRaises(ValueError):panel.alarm_mask_png('first','../../etc/passwd','reference')

if __name__ == '__main__':unittest.main()
