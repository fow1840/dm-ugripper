import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import inspection as module

class InspectionTests(unittest.TestCase):
    def hold(self, check, value, start):
        for i in range(40):check.update(value % 65536,start+i*.02)
        return start+1

    def test_five_cycles_and_stationary_not_double_counted(self):
        c=module.CycleCheck();now=self.hold(c,-4000,0)
        for i in range(5):
            now=self.hold(c,0,now)
            now=self.hold(c,-4000,now)
        self.assertEqual(c.phase,'done')
        self.assertEqual(len(c.rows),10)
        self.assertTrue(all(r['status']=='pass' for r in c.rows))
        self.hold(c,-4000,now)
        self.assertEqual(len(c.rows),10)

    def test_bad_endpoints_are_counted_and_codes_follow_open_amplitude(self):
        for closed,opened,closed_code,open_code in [(-151,-4201,'closed_below_range','open_above_range'),(151,-3799,'closed_above_range','open_below_range'),(-150,-4200,None,None),(150,-3800,None,None)]:
            c=module.CycleCheck();now=self.hold(c,-4000,0)
            now=self.hold(c,closed,now);self.hold(c,opened,now)
            self.assertEqual([r['code'] for r in c.rows],[closed_code,open_code])

    def test_crossing_zero_and_unstable_samples_do_not_count(self):
        c=module.CycleCheck();now=self.hold(c,-4000,0)
        for i in range(100):c.update((0 if i%2 else -300)%65536,now+i*.02)
        self.assertEqual(c.rows,[])
        self.assertEqual(c.phase,'close')

    def test_stop_never_stops_other_recording(self):
        with patch.object(module,'read_json',return_value={'test_request_id':'other'}),patch.object(module,'control') as control:
            module.stop_owned('mine')
            control.assert_not_called()

    def test_command_is_session_bound_and_has_no_countdown(self):
        with patch.object(module,'send') as send,patch.object(module.time,'time',return_value=100):
            module.control('test_start','a'*32)
            payload=json.loads(send.call_args.args[0])
            self.assertEqual(payload,dict(action='test_start',id='a'*32,dataset='trajectory_test',expires_ms=105000))

    def test_cancel_wrong_kind_or_id_does_not_signal(self):
        with patch.object(module,'JOB',{'id':'a','kind':'encoder'}):
            module.STOP.clear()
            with self.assertRaises(ValueError):module.stop('trajectory','a')
            self.assertFalse(module.STOP.is_set())

if __name__=='__main__':unittest.main()
