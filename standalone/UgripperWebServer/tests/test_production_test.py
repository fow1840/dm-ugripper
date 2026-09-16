import contextlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import production_test as production
import encoder_probe as encoder


class ProductionTests(unittest.TestCase):
    def test_threshold_boundaries(self):
        for a,b,expected in [(0,0,True),(2,1,True),(3,0,False),(0,2,False),(2,2,False),(4,0,False)]:
            rows=[dict(group=g,status='failed') for g,n in [('A',a),('B',b)] for _ in range(n)]
            self.assertEqual(production.verdict(rows),({'A':a,'B':b},expected))

    def test_all_62_rounds_continue_after_failures_and_include_final_pause(self):
        calls=[];states=[]
        def run(group,number,duration,pause,future):
            calls.append((group,number,duration,pause,future))
            production.JOB['rounds'].append(dict(group=group,status='failed'))
        production.JOB.clear();production.JOB['rounds']=[];production.CANCEL.clear()
        stack=Mock()
        with patch.object(production,'run_round',side_effect=run),patch.object(production,'save',side_effect=lambda **kw:states.append(kw)),patch.object(production,'MUTEX') as mutex:
            production.worker(stack)
        self.assertEqual(len(calls),62)
        self.assertEqual(calls[0],('A',1,120,20,10520))
        self.assertEqual(calls[50][:4],('A',51,120,20))
        self.assertEqual(calls[-1],('B',11,300,20,0))
        self.assertEqual(states[-1]['status'],'failed')
        stack.close.assert_called_once();mutex.release.assert_called_once()

    def test_cancel_never_passes_and_releases(self):
        production.CANCEL.set();stack=Mock()
        with patch.object(production,'save') as save,patch.object(production,'MUTEX') as mutex,patch.object(production,'run_round') as run:
            production.worker(stack)
        self.assertEqual(save.call_args.kwargs['status'],'cancelled');run.assert_not_called()
        stack.close.assert_called_once();mutex.release.assert_called_once();production.CANCEL.clear()

    def test_metadata_success_alone_cannot_hide_missing_files_or_short_recording(self):
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'episode_test';path.mkdir()
            meta=dict(quality_check_status='success',collection_duration_s=120,require_files=['sensor_left.mcap'])
            (path/'metadata.json').write_text(json.dumps(meta))
            self.assertEqual(production.check_episode(path,120)['status'],'failed')
            (path/'sensor_left.mcap').write_bytes(b'data')
            self.assertEqual(production.check_episode(path,120)['status'],'success')
            meta['collection_duration_s']=12;(path/'metadata.json').write_text(json.dumps(meta))
            self.assertEqual(production.check_episode(path,120)['status'],'failed')
            (path/'validation_error.log').write_text('camera gap')
            self.assertIn('camera gap',production.check_episode(path,120)['reason'])

    def test_interrupted_round_is_persisted_as_failure(self):
        old=dict(status='running',rounds=[dict(status='running',group='A')])
        with patch.object(production,'read_json',return_value=old),patch.object(production,'save') as save:
            production.recover()
        self.assertEqual(production.JOB['rounds'][0]['status'],'failed')
        self.assertEqual(save.call_args.kwargs['status'],'interrupted')


class EncoderStreamTests(unittest.TestCase):
    def test_stream_closes_both_ports_when_consumer_closes(self):
        attrs=[0,0,0,0,0,0,[0]*32]
        with patch.object(encoder,'operation_lock',contextlib.nullcontext),patch.object(encoder.os,'open',side_effect=[10,11]),patch.object(encoder.os,'close') as close,patch.object(encoder.termios,'tcgetattr',side_effect=lambda fd:[*attrs[:6],list(attrs[6])]),patch.object(encoder.termios,'tcsetattr'),patch.object(encoder,'read_position',return_value=1234):
            stream=encoder.stream_encoders();sample=next(stream)
            self.assertEqual(sample['left']['position'],1234);close.assert_not_called()
            stream.close();self.assertEqual({c.args[0] for c in close.call_args_list},{10,11})

    def test_single_missed_sample_does_not_end_stream(self):
        attrs=[0,0,0,0,0,0,[0]*32]
        with patch.object(encoder,'operation_lock',contextlib.nullcontext),patch.object(encoder.os,'open',side_effect=[10,11]),patch.object(encoder.os,'close'),patch.object(encoder.termios,'tcgetattr',side_effect=lambda fd:[*attrs[:6],list(attrs[6])]),patch.object(encoder.termios,'tcsetattr'),patch.object(encoder,'read_position',side_effect=[1234,1234,None,1234,1234,1234]):
            stream=encoder.stream_encoders()
            first=next(stream);second=next(stream);stream.close()
            self.assertEqual(first['left']['status'],'unavailable')
            self.assertEqual(second['left']['status'],'ok')
            self.assertEqual(second['dropped']['left'],1)

    def test_zero_duration_releases_without_streaming(self):
        attrs=[0,0,0,0,0,0,[0]*32]
        with patch.object(encoder,'operation_lock',contextlib.nullcontext),patch.object(encoder.os,'open',side_effect=[10,11]),patch.object(encoder.os,'close') as close,patch.object(encoder.termios,'tcgetattr',side_effect=lambda fd:[*attrs[:6],list(attrs[6])]),patch.object(encoder.termios,'tcsetattr'),patch.object(encoder,'read_position',return_value=1234):
            self.assertEqual(list(encoder.stream_encoders(duration=0)),[])
            self.assertEqual(close.call_count,2)

if __name__=='__main__':unittest.main()
