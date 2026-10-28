from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import inspection as m

class ConsistencyTests(unittest.TestCase):
    def check(self,closed,opened):
        c=m.CycleCheck()
        for a,b in zip(closed,opened):
            c.record_endpoint(a,True);c.record_endpoint(b,False)
        return c

    def test_hardware_rules_use_each_gripper_sn_not_device_version(self):
        for l,r,expected in [('1','1',{'left':2,'right':2}),('3','3',{'left':1,'right':1}),('1','3',{'left':2,'right':1}),('3','1',{'left':1,'right':2})]:
            cache={'updated_at_ms':100000,'device_versions':{'hardware_version':'v9.9.9'},'gripper':{
                'left':{'connected':True,'serial':'DAGZ'+l+'22676001465'},
                'right':{'connected':True,'serial':'DAGY'+r+'226760096E2'}}}
            with patch.object(m,'read_json',return_value=cache),patch.object(m.time,'time',return_value=100):
                identities,limits,failures=m.encoder_hardware_rule()
            self.assertEqual(limits,expected)
            self.assertEqual(failures,[] if l==r else [dict(side='left' if l=='1' else 'right',code='gripper_version_mismatch')])
        cache['updated_at_ms']=0
        with patch.object(m,'read_json',return_value=cache),patch.object(m.time,'time',return_value=100):
            self.assertEqual(m.encoder_hardware_rule()[1],dict(left=None,right=None))

    def test_bad_or_disconnected_sn_does_not_fall_back_to_umi_version(self):
        for sn,connected in [('',True),('DAG',True),('DAGZ222676001465',True),('DAGZ322676001465',False)]:
            cache={'updated_at_ms':100000,'device_versions':{'hardware_version':'v2.9.0'},'gripper':{'left':dict(serial=sn,connected=connected)}}
            with patch.object(m,'read_json',return_value=cache),patch.object(m.time,'time',return_value=100):
                identities,limits,failures=m.encoder_hardware_rule()
            self.assertIsNone(limits['left'])
            self.assertEqual(len(failures),2)

    def test_both_degree_limits_use_unrounded_spread(self):
        for limit,passing,failing in [(2,364,365),(1,182,183)]:
            for spread,expected in [(passing,'pass'),(failing,'fail')]:
                c=self.check([0,0,0,0,spread],[-4000]*5)
                result=m.encoder_report({'left':c},limit)
                self.assertEqual(result['sides']['left']['consistency'][0]['status'],expected)
        # Exact equality remains a pass, including if future rules change.
        c=self.check([0,0,0,0,182],[-4000]*5)
        self.assertEqual(m.encoder_report({'left':c},182*360/65536)['sides']['left']['consistency'][0]['status'],'pass')

    def test_all_sides_endpoints_and_range_failures_are_retained(self):
        c=self.check([-200,200,0,0,0],[-4300,-3600,-4000,-4000,-4000])
        report=m.encoder_report({'left':c,'right':c},1)
        for side in ('left','right'):
            failures=[r for r in report['failures'] if r.get('side')==side]
            self.assertEqual(len(failures),6)
            self.assertEqual({r['code'] for r in failures},{'closed_below_range','closed_above_range','open_above_range','open_below_range','closed_consistency_exceeded','open_consistency_exceeded'})
        self.assertEqual(len(report['failures']),12)

    def test_consistency_compares_same_endpoint_across_rounds(self):
        c=self.check([50]*5,[-4100]*5)
        report=m.encoder_report({'left':c},1)
        self.assertEqual(report['failures'],[])
        self.assertEqual([x['max_difference_deg'] for x in report['sides']['left']['consistency']],[0,0])

    def test_partial_or_unknown_rule_never_passes_consistency(self):
        c=self.check([0]*4,[-4000]*4)
        self.assertEqual(m.encoder_report({'left':c},2)['sides']['left']['consistency'][0]['status'],'pending')
        self.assertEqual(len(m.encoder_report({'left':c},2,terminal=True)['failures']),2)
        c=self.check([0]*5,[-4000]*5)
        report=m.encoder_report({'left':c},None,'hardware_version_unsupported')
        self.assertEqual(report['failures'],[{'code':'hardware_version_unsupported'}])
        self.assertEqual(report['sides']['left']['consistency'][0]['status'],'unavailable')

    def test_worker_finishes_all_rounds_and_reports_multiple_failures(self):
        values=[]
        for value in [-4000]+[v for _ in range(5) for v in (300,-4300)]+[-3000]:
            values.extend([dict(left={'status':'ok','position':value%65536},right={'status':'ok','position':value%65536})]*20)
        state={}
        def save(**fields):state.update(fields)
        from itertools import count
        ticks=count(0,.02)
        with patch.object(m,'encoder_completion_feedback') as feedback,patch.object(m,'recorder_session',return_value='session-a'),patch.object(m,'save',side_effect=save),patch.object(m,'encoder_hardware_rule',return_value=('v2.5.0',2,None)),patch.object(m,'stream_encoders',return_value=(v for v in values)),patch.object(m.time,'monotonic',side_effect=lambda:next(ticks)),patch.object(m.STOP,'is_set',return_value=False):
            m.encoder_worker(('left','right'))
        self.assertEqual(state['result'],'fail')
        self.assertEqual(state['status'],'completed')
        self.assertEqual(len(state['failures']),20)
        self.assertEqual([c.args[0] for c in feedback.call_args_list],['left','right'])
        for side in ('left','right'):
            self.assertEqual(state['sides'][side]['completed_cycles'],5)
            self.assertEqual(len(state['sides'][side]['checks']),10)

    def test_runtime_restart_interrupts_and_closes_sampler(self):
        state={};closed=[]
        def samples():
            try:
                while True:yield {s:dict(status='ok',position=61536) for s in ('left','right')}
            finally:closed.append(True)
        with patch.object(m,'recorder_session',side_effect=['old','new']),patch.object(m,'save',side_effect=lambda **v:state.update(v)),patch.object(m,'encoder_hardware_rule',return_value=('v2.5.0',2,None)),patch.object(m,'stream_encoders',return_value=samples()),patch.object(m.STOP,'is_set',return_value=False):
            m.encoder_worker(('left','right'))
        self.assertEqual(state['status'],'interrupted')
        self.assertEqual(state['code'],'service_interrupted')
        self.assertTrue(closed)
        self.assertTrue(any(f['code']=='service_interrupted' for f in state['failures']))

    def test_completion_feedback_binds_job_side_and_expiry(self):
        import json
        with patch.object(m,'JOB',{'id':'a'*32}),patch.object(m,'send') as send,patch.object(m.time,'time',return_value=100):
            m.encoder_completion_feedback('left')
        self.assertEqual(json.loads(send.call_args.args[0]),dict(action='encoder_side_done',id='a'*32,side='left',expires_ms=105000))
        with patch.object(m,'JOB',{'id':'a'*32}),patch.object(m,'send',side_effect=OSError('offline')):
            m.encoder_completion_feedback('right')
