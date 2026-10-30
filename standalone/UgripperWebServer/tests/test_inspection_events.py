import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch, MagicMock
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import inspection_events as events
import inspection

class EventsTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        for target,name,value in [(events,'HISTORY',self.root/'history'),(inspection,'RESULT',self.root/'latest.json'),(inspection,'JOB',{})]:
            p=patch.object(target,name,value);p.start();self.addCleanup(p.stop)
        p=patch.dict(os.environ,UGRIPPER_INSPECTION_CALLBACK_URL='http://127.0.0.1/events');p.start();self.addCleanup(p.stop)
        inspection.JOB.update(id='a'*32,kind='trajectory',status='running',phase='starting')

    def test_history_compact_and_retention(self):
        with patch.object(events,'MAX_REPORTS',2):
            for n in range(3):
                events.store(dict(id=f'{n:032x}',kind='trajectory',status='running',phase='recording'))
                os.utime(events.report_path(f'{n:032x}'),(1000+n,1000+n))
        self.assertFalse(events.lookup('0'*32))
        self.assertEqual(events.compact(events.lookup(f'{1:032x}')),dict(id=f'{1:032x}',status='recording',episode=None,code=None))
        with self.assertRaises(ValueError):events.lookup('../x')

    def test_start_failure_has_only_start_event(self):
        with patch.object(inspection.RecordingRequest,'start',side_effect=inspection.ControlError('ego_sync_pending')),patch.object(inspection.RecordingRequest,'stop'):
            inspection.trajectory_worker()
        job=events.lookup('a'*32)
        self.assertEqual(list(job['events']),['started'])
        self.assertEqual(job['events']['started']['status'],'failed')
        self.assertEqual(job['code'],'ego_sync_pending')

    def test_actual_start_then_stop_notifications(self):
        final=self.root/'episode';final.mkdir()
        (final/'metadata.json').write_text(json.dumps(dict(quality_check_status='success',test_dataset_name='trajectory_test')))
        real_read=inspection.read_json
        def read(path):
            return dict(test_request_id='a'*32,episode_dir=str(final)+'-temp',state='recording') if path==inspection.RECORD_LOCK else real_read(path)
        with patch.object(inspection.RecordingRequest,'start',return_value=Path(str(final)+'-temp')),patch.object(inspection,'read_json',side_effect=read),patch.object(inspection.STOP,'wait',return_value=True),patch.object(inspection.RecordingRequest,'stop'):
            inspection.trajectory_worker()
        job=events.lookup('a'*32)
        self.assertEqual([v['status'] for v in job['events'].values()],['recording','completed'])
        self.assertEqual(job['events']['finished']['episode'],str(final))

    def test_retry_order_ack_and_persistent_dedup(self):
        inspection.notify('started','recording')
        inspection.notify('finished','completed')
        opener=MagicMock()
        response=MagicMock();response.__enter__.return_value.status=204
        opener.open.side_effect=[OSError('offline'),response,response]
        with patch.object(events.urllib.request,'build_opener',return_value=opener):
            events.deliver_once(now=100)
            self.assertEqual(opener.open.call_count,1)
            events.deliver_once(now=100.5)
            self.assertEqual(opener.open.call_count,1)
            events.deliver_once(now=101)
            self.assertEqual(opener.open.call_count,3)
            events.deliver_once(now=999)
            self.assertEqual(opener.open.call_count,3)
        sent=[json.loads(c.args[0].data)['event'] for c in opener.open.call_args_list]
        self.assertEqual(sent,['trajectory_recording_started']*2+['trajectory_recording_finished'])

    def test_retries_are_bounded(self):
        inspection.notify('started','failed','start_not_accepted')
        opener=MagicMock();opener.open.side_effect=OSError('offline')
        with patch.object(events.urllib.request,'build_opener',return_value=opener):
            for now in range(0,1000,100):events.deliver_once(now=now)
        self.assertEqual(opener.open.call_count,5)

    def test_recovery_fills_terminal_notification_gap(self):
        inspection.save(status='failed',code='start_not_accepted')
        inspection.recover()
        self.assertEqual(events.lookup('a'*32)['events']['started']['status'],'failed')
        inspection.recover()
        self.assertEqual(len(events.lookup('a'*32)['events']),1)

    def test_start_failure_preserves_specific_reason(self):
        with patch.object(inspection.RecordingRequest,'start',side_effect=inspection.ControlError('start_timeout')), patch.object(inspection.RecordingRequest,'stop'):
            inspection.trajectory_worker()
        job=events.lookup('a'*32)
        self.assertEqual(list(job['events']),['started'])
        self.assertEqual(job['code'],'start_timeout')

    def test_finalization_timeout_never_reports_completed(self):
        with patch.object(inspection.RecordingRequest,'start',return_value=self.root/'episode-temp'),patch.object(inspection.STOP,'wait',return_value=True),patch.object(inspection.RecordingRequest,'stop',side_effect=inspection.ControlError('finalization_timeout')):
            inspection.trajectory_worker()
        job=events.lookup('a'*32)
        self.assertEqual(job['events']['started']['status'],'recording')
        self.assertEqual(job['events']['finished'],dict(event='trajectory_recording_finished',id='a'*32,status='failed',code='finalization_timeout',episode=None))

    def test_cleanup_failure_does_not_replace_start_failure(self):
        with patch.object(inspection.RecordingRequest,'start',side_effect=inspection.ControlError('ego_sync_pending')), patch.object(inspection.RecordingRequest,'stop',side_effect=inspection.ControlError('finalization_timeout')):
            inspection.trajectory_worker()
        job=events.lookup('a'*32)
        self.assertEqual(job['code'], 'ego_sync_pending')
        self.assertEqual(job['cleanup_code'], 'finalization_timeout')

    def test_restart_recovery_ends_encoder_task(self):
        inspection.save(kind='encoder',status='running',failures=[dict(code='closed_below_range')])
        inspection.recover()
        job=inspection.snapshot()
        self.assertEqual(job['status'],'interrupted')
        self.assertEqual(job['phase'],'finished')
        self.assertEqual(job['code'],'service_interrupted')
        self.assertEqual(len(job['failures']),2)
        self.assertFalse(inspection.MUTEX.locked())

    def test_encoder_tasks_only_overwrite_latest_snapshot(self):
        for n in range(20):
            inspection.JOB.clear()
            inspection.JOB.update(id=f'{n:032x}',kind='encoder')
            for status in ('running','completed','cancelled','interrupted'):
                inspection.save(status=status)
        self.assertEqual(inspection.snapshot()['id'],f'{19:032x}')
        self.assertEqual(list(events.HISTORY.glob('*')),[])
        self.assertFalse(inspection.RESULT.with_suffix('.new').exists())

    def test_startup_cleans_legacy_encoder_and_partial_files_preserving_trajectory(self):
        events.HISTORY.mkdir()
        events.atomic_json(events.report_path('b'*32),dict(id='b'*32,kind='encoder',status='cancelled'))
        events.store(dict(id='c'*32,kind='trajectory',status='completed'))
        (events.HISTORY/('d'*32+'.new')).write_text('{partial')
        inspection.save(id='b'*32,kind='encoder',status='running')
        inspection.RESULT.with_suffix('.new').write_text('{partial')
        inspection.recover()
        self.assertEqual(inspection.snapshot()['status'],'interrupted')
        self.assertFalse(events.lookup('b'*32))
        self.assertTrue(events.lookup('c'*32))
        self.assertEqual([p.name for p in events.HISTORY.iterdir()],['c'*32+'.json'])
        self.assertFalse(inspection.RESULT.with_suffix('.new').exists())
