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
        self.assertEqual(len(c.rows),9)
        now=self.hold(c,-3000,now)
        self.assertEqual(c.phase,'done')
        self.assertEqual(len(c.rows),10)
        self.assertTrue(all(r['status']=='pass' for r in c.rows))
        self.hold(c,-4000,now)
        self.assertEqual(len(c.rows),10)

    def test_bad_endpoints_are_counted_and_codes_follow_open_amplitude(self):
        for closed,opened,closed_code,open_code in [(-101,-4201,'closed_below_range','open_above_range'),(151,-3649,'closed_above_range','open_below_range'),(-100,-4200,None,None),(150,-3650,None,None)]:
            c=module.CycleCheck();now=self.hold(c,-4000,0)
            now=self.hold(c,closed,now);now=self.hold(c,opened,now);self.hold(c,-3000,now)
            self.assertEqual([r['code'] for r in c.rows],[closed_code,open_code])

    def test_crossing_zero_and_unstable_samples_do_not_count(self):
        c=module.CycleCheck();now=self.hold(c,-4000,0)
        for i in range(100):c.update((0 if i%2 else -300)%65536,now+i*.02)
        self.assertEqual(c.rows,[])
        self.assertEqual(c.phase,'close')

    def test_mid_travel_pauses_only_commit_peak_on_reversal(self):
        c=module.CycleCheck();now=self.hold(c,-4000,0)
        for value in (-3000,-2000,-1000,0):now=self.hold(c,value,now)
        self.assertEqual(c.rows,[])
        # A brief true maximum is retained even if the operator relaxes slightly.
        c.update(120,now);now+=.02
        now=self.hold(c,0,now)
        self.assertEqual(c.rows,[])
        now=self.hold(c,-600,now)
        self.assertEqual([r['value'] for r in c.rows],[120])
        for value in (-1500,-2500,-3500,-4000):now=self.hold(c,value,now)
        self.assertEqual(len(c.rows),1)
        c.update((-4100)%65536,now);now+=.02
        now=self.hold(c,-4000,now)
        self.assertEqual(len(c.rows),1)
        self.hold(c,-3000,now)
        self.assertEqual([r['value'] for r in c.rows],[120,-4100])

    def test_continuous_motion_needs_no_endpoint_dwell(self):
        c=module.CycleCheck();now=0
        for values in (range(-4000,1,100),range(0,-4001,-100),range(-4000,-2000,100)):
            for value in values:
                c.update(value%65536,now);now+=.02
        self.assertEqual([r['value'] for r in c.rows],[0,-4000])

    def test_short_reverse_and_missing_sample_do_not_confirm(self):
        c=module.CycleCheck();now=self.hold(c,-4000,0);now=self.hold(c,0,now)
        c.update((-600)%65536,now)
        c.interrupt()
        c.update((-600)%65536,now+1)
        self.assertEqual(c.rows,[])
        c.update(0,now+1.02)
        self.assertEqual(c.rows,[])
        self.hold(c,-1000,now+2)
        self.assertEqual([r['value'] for r in c.rows],[0])

    def test_both_sides_required(self):
        for side in ('left','right'):
            with self.assertRaisesRegex(ValueError,'inspection_requires_both_sides'):
                module.start('encoder',side)

    def test_two_hands_progress_independently(self):
        left,right=module.CycleCheck(),module.CycleCheck()
        now=self.hold(left,-4000,0);self.hold(right,-4000,0)
        for _ in range(5):
            now=self.hold(left,0,now);now=self.hold(left,-4000,now)
        self.hold(left,-3000,now)
        self.assertEqual(left.phase,'done')
        self.assertEqual(right.report()['completed_cycles'],0)

    def test_small_open_region_round_trip_does_not_count_as_closed(self):
        c=module.CycleCheck();now=self.hold(c,-4173,0)
        for value in (-3420,-4173,-3420,-4173):now=self.hold(c,value,now)
        self.assertEqual(c.rows,[])
        self.assertEqual(c.report()['completed_cycles'],0)
        now=self.hold(c,-41,now);now=self.hold(c,-4173,now)
        self.assertEqual([r['value'] for r in c.rows],[-41])
        self.hold(c,-3000,now)
        self.assertEqual([r['value'] for r in c.rows],[-41,-4173])

    def test_small_closed_region_round_trip_does_not_count_as_open(self):
        c=module.CycleCheck();now=self.hold(c,-4000,0);now=self.hold(c,0,now)
        now=self.hold(c,-700,now)
        self.assertEqual(len(c.rows),1)
        for value in (0,-700,0,-700):now=self.hold(c,value,now)
        self.assertEqual(len(c.rows),1)
        now=self.hold(c,-4000,now);self.hold(c,-3000,now)
        self.assertEqual([r['value'] for r in c.rows],[0,-4000])

    def test_endpoint_recognition_does_not_hide_out_of_range_peaks(self):
        c=module.CycleCheck();now=self.hold(c,-4000,0)
        now=self.hold(c,-250,now);now=self.hold(c,-3500,now);self.hold(c,-2000,now)
        self.assertEqual([r['code'] for r in c.rows],['closed_below_range','open_below_range'])

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
