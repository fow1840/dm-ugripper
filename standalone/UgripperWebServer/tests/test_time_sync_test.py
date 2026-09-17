import contextlib
import fcntl
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import time_sync_test as sync

class TimeSyncTests(unittest.TestCase):
    def tearDown(self):
        sync.CANCEL.clear()

    def test_validation(self):
        self.assertEqual(sync.parameters({}),dict(duration_s=100,count=1,delay_s=2))
        self.assertEqual(sync.parameters(dict(duration_s=10,count=2,delay_s=0))['delay_s'],0)
        for values in [dict(duration_s=0),dict(count=101),dict(count=True),dict(delay_s=1.2),dict(delay_s='2'),dict(duration_s=2),dict(delay_s=-1)]:
            with self.subTest(values=values),self.assertRaises(ValueError):sync.parameters(values)

    def test_exclusive_guard_blocks_start_without_touching_recorder(self):
        with tempfile.TemporaryDirectory() as root:
            lock=Path(root)/'production.lock';lock.touch()
            with lock.open() as fd,patch.object(sync,'PRODUCTION_LOCK',lock),patch.object(sync,'command') as command:
                fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
                with self.assertRaises(sync.Busy):sync.start({})
                command.assert_not_called()
            self.assertFalse(sync.MUTEX.locked())

    def test_cancel_stale_id(self):
        with self.assertRaises(ValueError):sync.cancel('stale')

    def test_worker_runs_requested_count(self):
        sync.JOB.clear();sync.JOB.update(settings=dict(count=3))
        stack=Mock()
        with patch.object(sync,'run_round') as run,patch.object(sync,'save') as save,patch.object(sync,'MUTEX') as mutex:
            sync.worker(stack)
            self.assertEqual([c.args[0] for c in run.call_args_list],[1,2,3])
            self.assertEqual(save.call_args.kwargs['status'],'completed')
            stack.close.assert_called_once();mutex.release.assert_called_once()

    def test_failure_stops_following_rounds(self):
        sync.JOB.clear();sync.JOB.update(settings=dict(count=3))
        with patch.object(sync,'run_round',side_effect=RuntimeError('bad')) as run,patch.object(sync,'save') as save,patch.object(sync,'MUTEX'):
            sync.worker(Mock())
            run.assert_called_once_with(1)
            self.assertEqual(save.call_args.kwargs['status'],'failed')

    def test_cancel_before_start(self):
        sync.JOB.clear();sync.JOB.update(settings=dict(count=3));sync.CANCEL.set()
        with patch.object(sync,'run_round') as run,patch.object(sync,'save') as save,patch.object(sync,'MUTEX'):
            sync.worker(Mock())
            run.assert_not_called();self.assertEqual(save.call_args.kwargs['status'],'cancelled')

    def test_recovery_stops_owned_episode_and_resets_gpio(self):
        old=dict(status='running',rounds=[dict(status='running',episode='/data/episode-temp')])
        with patch.object(sync,'read_json',return_value=old),patch.object(sync,'reset_gpio') as reset,patch.object(sync,'stop_owned') as stop,patch.object(sync,'save') as save:
            sync.recover()
            reset.assert_called_once();stop.assert_called_once_with(Path('/data/episode-temp'))
            self.assertEqual(save.call_args.kwargs['status'],'interrupted')

    def test_cancel_live_subprocess_waits_for_cleanup_and_releases_round(self):
        import threading
        with tempfile.TemporaryDirectory() as root:
            root=Path(root);(root/'time-sync-results').mkdir()
            helper=root/'helper.sh'
            helper.write_text('trap "echo cleaned; exit 0" TERM\necho WEB_PHASE=recording:30\nsleep 30 &\nwait $!\n')
            sync.JOB.clear();sync.JOB.update(id='test',rounds=[],settings=dict(duration_s=30,delay_s=2,count=1))
            timer=threading.Timer(0.2,sync.CANCEL.set)
            with patch.object(sync,'STATE',root),patch.object(sync,'SCRIPT',helper),patch.object(sync,'RECORD_LOCK',root/'absent'),patch.object(sync,'save'),patch.object(sync,'reset_gpio') as reset:
                timer.start()
                try:
                    with self.assertRaises(InterruptedError):sync.run_round(1)
                finally:timer.join()
                self.assertEqual(sync.JOB['rounds'][0]['status'],'cancelled')
                self.assertIn('cleaned',sync.JOB['rounds'][0]['log'])
                reset.assert_called_once()

    def test_recovery_gpio_failure_still_stops_owned_recording(self):
        old=dict(status='running',rounds=[dict(status='running',episode='/data/episode-temp')])
        with patch.object(sync,'read_json',return_value=old),patch.object(sync,'reset_gpio',side_effect=OSError('GPIO failed')),patch.object(sync,'stop_owned') as stop,patch.object(sync,'save') as save:
            sync.recover()
            stop.assert_called_once_with(Path('/data/episode-temp'))
            self.assertIn('GPIO failed',save.call_args.kwargs['reason'])
