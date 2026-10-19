import builtins
import contextlib
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import boot_firmware as boot
import common
import maintenance as worker
import upgrade_state as state


class ExternalUpgradeTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.packages=self.root/'packages'; self.packages.mkdir()
        self.private=self.root/'private'; self.private.mkdir()
        self.run=self.root/'run'; self.run.mkdir()
        self.pending=self.root/'pending.json'
        self.bootid=self.root/'boot-id'; self.bootid.write_text('boot-one')
        self.guard=self.root/'reboot-guard'
        self.bms=self.root/'bms.txt'
        self.bms.write_text(f'timestamp: {int(time.time())}\nsoc_percent: 50\n')
        for module, key, value in [
            (worker,'STATE',self.root),(worker,'PRIVATE',self.private),(worker,'RUN',self.run),
            (worker,'PACKAGES',self.packages),(worker,'REBOOT_GUARD',self.guard),
            (worker,'MUTEX',threading.Lock()),(worker,'JOB',{}),(worker,'SOFTWARE_RESTART_PENDING',False),
            (state,'STATE',self.root),(state,'PACKAGES',self.packages),
            (state,'BOOT_ID',self.bootid),(state,'REBOOT_GUARD',self.guard),
            (boot,'STATE',self.root),(boot,'BOOT_ID',self.bootid),(boot,'PENDING',self.pending),(boot,'BMS_SOURCE',self.bms),
            (common,'LOCK',self.root/'lock'),(common,'PRODUCTION_LOCK',self.root/'production.lock')]:
            p=patch.object(module,key,value); p.start(); self.addCleanup(p.stop)
        common.PRODUCTION_LOCK.touch()
        p=patch.object(worker,'open',side_effect=lambda *a,**k: builtins.open(self.root/'usb.lock','a'),create=True)
        p.start(); self.addCleanup(p.stop)

    def test_external_request_has_no_confirmation_and_forces(self):
        with patch.object(worker,'submit',return_value={'id':'test'}) as submit:
            worker.external_submit({'kind':'main_camera','package':'camera.img','force':False,'confirm':False})
            self.assertEqual(submit.call_args.args[0],dict(kind='main_camera',package='camera.img',sha256=None,external=True,force=True))
        with self.assertRaises(ValueError): worker.external_submit({'kind':'zeroing','package':'x'})
        worker.JOB.update(id='a'*32,external=True)
        with patch.object(worker.VERSION_CONFIRM_EVENT,'wait') as wait:
            self.assertTrue(worker.confirm_camera_version_failure('left','after_flash','no version'))
            wait.assert_not_called()
        self.assertEqual(len(worker.JOB['version_checks_skipped']),1)

    def test_staging_and_success_are_persisted_by_task(self):
        source=self.packages/'app.deb'; source.write_bytes(b'package bytes')
        digest=hashlib.sha256(source.read_bytes()).hexdigest()
        with patch.object(worker.threading,'Thread') as thread:
            accepted=worker.external_submit({'kind':'software','package':source.name,'sha256':digest})
            args=thread.call_args.kwargs['args']
            self.assertFalse(source.exists()); self.assertEqual(args[1].read_bytes(),b'package bytes')
            self.assertTrue(args[3])
        with patch.object(worker,'command',return_value=Mock(stdout='inactive',returncode=0)), patch.object(worker,'execute',return_value={'version':'2.1.15'}):
            worker.worker(*args)
        result=state.task_result(accepted['id'])
        self.assertEqual(result['status'],'success'); self.assertTrue(result['success'])
        self.assertTrue(result['upload_cleaned']); self.assertFalse(args[1].exists())
        with self.assertRaisesRegex(common.Busy,'软件更新'):
            worker.external_submit({'kind':'software','package':'next.deb'})
        latest=dict(result,id='b'*32); state.save_task(latest)
        self.assertEqual(state.task_result(accepted['id'])['id'],accepted['id'])
        self.assertEqual(state.task_result('c'*32),{})

    def test_bad_names_symlinks_hash_and_busy_never_execute(self):
        (self.packages/'target').write_bytes(b'good')
        (self.packages/'link.deb').symlink_to(self.packages/'target')
        with patch.object(worker.threading,'Thread') as thread:
            for name in ('../target','/tmp/a','sub/a','link.deb'):
                with self.subTest(name=name),self.assertRaises((ValueError,OSError)):
                    worker.external_submit({'kind':'software','package':name})
            with self.assertRaisesRegex(ValueError,'sha256'):
                worker.external_submit({'kind':'software','package':'target','sha256':'0'*64})
            self.assertTrue((self.packages/'target').exists())
            with common.operation_lock(), self.assertRaises(common.Busy):
                worker.external_submit({'kind':'software','package':'target'})
            thread.assert_not_called()
        self.assertTrue(worker.MUTEX.acquire(False)); worker.MUTEX.release()

    def test_battery_drop_after_acceptance_rejects_before_stopping_services(self):
        worker.JOB.update(id='a'*32,status='running')
        worker.MUTEX.acquire()
        self.bms.write_text(f'timestamp: {int(time.time())}\nsoc_percent: 29\n')
        with patch.object(worker,'command') as command,patch.object(worker,'execute') as execute:
            worker.worker('main_camera',None,contextlib.ExitStack())
            command.assert_not_called();execute.assert_not_called()
        self.assertFalse(worker.JOB['success'])
        self.assertIn('30%',worker.JOB['message'])

    def test_all_upgrade_execute_paths_check_battery_before_hardware_access(self):
        self.bms.write_text(f'timestamp: {int(time.time())}\nsoc_percent: 29\n')
        for kind in ('software','main_camera','gripper','rk3588_boot'):
            with self.subTest(kind=kind),patch.object(worker,'run') as run,patch.object(worker,'camera_identity') as identity:
                with self.assertRaisesRegex(ValueError,'30%'):worker.execute(kind,Path('unused'))
                run.assert_not_called();identity.assert_not_called()

    def test_software_rechecks_after_package_validation_before_install(self):
        def run(args,*rest):
            if args[0]=='dpkg-deb' and args[1]=='-f':
                return 'Package: ugripper\nArchitecture: arm64\nVersion: 2.1.15\n'
            return ''
        with patch.object(boot,'require_battery',side_effect=[50,ValueError('battery dropped below 30%')]),patch.object(worker,'report'),patch.object(worker,'run',side_effect=run) as command:
            with self.assertRaisesRegex(ValueError,'30%'):worker.execute('software',Path('app.deb'))
            self.assertFalse(any(c.args[0][:2]==['dpkg','--install'] for c in command.call_args_list))
            self.assertFalse((self.run/'self-installing').exists())

    def test_main_camera_rechecks_before_each_side(self):
        with patch.object(boot,'require_battery',side_effect=[50,50,ValueError('battery dropped below 30%')]),patch.object(worker,'report'),patch.object(Path,'exists',return_value=True),patch.object(worker.os,'access',return_value=True),patch.object(worker.dfu,'read_image_version'),patch.object(worker,'camera_identity',return_value={}),patch.object(worker.dfu,'MainCameraDfuUpdater') as updater:
            updater.return_value.upgrade.return_value.to_dict.return_value={'status':'success'}
            with self.assertRaisesRegex(ValueError,'30%'):worker.execute('main_camera',Path('cam.img'))
            updater.return_value.upgrade.assert_called_once()
            self.assertEqual(updater.return_value.upgrade.call_args.args[0],'left')

    def test_gripper_rechecks_before_each_side(self):
        worker.JOB.update(id='a'*32)
        with patch.object(boot,'require_battery',side_effect=[50,50,ValueError('battery dropped below 30%')]),patch.object(worker,'validate_hex'),patch.object(Path,'exists',return_value=True),patch.object(worker,'report'),patch.object(worker,'command',return_value=Mock(returncode=0,stdout='',stderr='')),patch.object(worker,'run',return_value='升级后版本: 1.2.3\n两次查询一致') as run:
            with self.assertRaisesRegex(ValueError,'30%'):worker.execute('gripper',Path('gripper.hex'))
            run.assert_called_once()
            self.assertIn('/dev/left_gripper',run.call_args.args[0])

    def pending_task(self):
        jobid='a'*32
        state.save_task(dict(id=jobid,kind='rk3588_boot',status='awaiting_reboot',success=True,reboot_pending=True))
        common.atomic_json(self.pending,dict(id=jobid,status='running',stage='awaiting_reboot',boot_id='boot-one'))
        return jobid

    def test_pending_boot_prevents_overwriting_task(self):
        self.pending_task()
        with self.assertRaises(common.Busy):worker.external_submit({'kind':'software','package':'app.deb'})

    def test_reboot_separate_command_cleans_store_and_retains_result(self):
        jobid=self.pending_task()
        (self.packages/'unused.img').write_text('remove')
        outside=self.root/'keep'; outside.write_text('keep')
        (self.packages/'link').symlink_to(outside)
        with patch.object(worker,'run',return_value='') as run,patch.object(worker.threading,'Thread') as thread:
            result=worker.reboot({'id':jobid})
            self.assertEqual(result['status'],'rebooting')
            self.assertTrue(self.guard.exists()); self.assertEqual(list(self.packages.iterdir()),[])
            self.assertTrue(outside.exists()); self.assertTrue(self.pending.exists())
            self.assertEqual(state.task_result(jobid)['status'],'rebooting')
            self.assertTrue(any('systemd-run' in c.args[0] for c in run.call_args_list))
            stack=thread.call_args.kwargs['args'][0]
            with self.assertRaises(common.Busy):
                with common.operation_lock(): pass
        with patch.object(worker.time,'sleep'):
            worker.reboot_watchdog(stack)
        self.assertFalse(self.guard.exists())
        self.assertEqual(state.task_result(jobid)['error_code'],'reboot_timeout')
        self.assertEqual(state.task_result(jobid)['status'],'awaiting_reboot')

    def test_reboot_rejects_recording_and_wrong_task_without_cleanup(self):
        jobid=self.pending_task(); source=self.packages/'keep'; source.touch()
        with common.operation_lock(),self.assertRaises(common.Busy): worker.reboot({'id':jobid})
        with self.assertRaisesRegex(ValueError,'task_not_awaiting_reboot'):worker.reboot({'id':'b'*32})
        self.assertTrue(source.exists())

    def test_reboot_schedule_failure_preserves_retryable_result(self):
        jobid=self.pending_task()
        with patch.object(worker,'run',side_effect=RuntimeError('schedule failed')):
            with self.assertRaises(RuntimeError):worker.reboot({'id':jobid})
        self.assertFalse(self.guard.exists()); self.assertTrue(self.pending.exists())
        self.assertEqual(state.task_result(jobid)['status'],'awaiting_reboot')
        self.assertTrue(worker.MUTEX.acquire(False)); worker.MUTEX.release()

    def test_package_cleanup_only_on_new_boot_not_service_restart(self):
        state.recover_package_store()
        source=self.packages/'new.deb';source.touch()
        state.recover_package_store();self.assertTrue(source.exists())
        self.bootid.write_text('boot-two')
        self.guard.touch();state.recover_package_store()
        self.assertFalse(source.exists());self.assertFalse(self.guard.exists())

    def test_no_terminal_success_before_service_restore(self):
        worker.JOB.update(id='a'*32,status='running')
        worker.MUTEX.acquire()
        calls=[]
        def command(args,*rest):
            if args[:2]==['systemctl','start']:
                calls.append(worker.JOB['status'])
                return Mock(returncode=1,stdout='')
            return Mock(returncode=0,stdout='active')
        with patch.object(worker,'command',side_effect=command),patch.object(worker,'run'),patch.object(worker,'execute',return_value={'ok':True}):
            worker.worker('main_camera',None,contextlib.ExitStack())
        self.assertEqual(calls,['running','running'])
        self.assertFalse(worker.JOB['success']); self.assertEqual(worker.JOB['status'],'failed')


if __name__ == '__main__':unittest.main()
