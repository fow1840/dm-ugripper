"""Exercise the real HTTP routes without a cookie or login bootstrap."""
import http.client
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
import json
from pathlib import Path
import runpy
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import server


class PlainHandler(server.Handler):
    # TLS is verified on-device; use HTTP for isolated route tests.
    def setup(self):
        BaseHTTPRequestHandler.setup(self)

    def log_message(self, *args):
        pass


class AnonymousAccessTests(unittest.TestCase):
    def setUp(self):
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), PlainHandler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection(*self.server.server_address, timeout=3)
        try:
            conn.request(method, path, body, headers or {})
            response = conn.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            conn.close()

    def test_page_and_status_without_credentials(self):
        code, headers, body = self.request('GET', '/')
        self.assertEqual(code, 200)
        self.assertIn(b'id="maintenancePasswordLabel" hidden', body)
        self.assertNotIn(b'daimon', body)
        self.assertNotIn('Set-Cookie', headers)
        with patch.object(server, 'status', return_value={'recording': False}):
            code, headers, body = self.request('GET', '/api/v1/status?details=true')
        self.assertEqual(code, 200)
        self.assertFalse(json.loads(body)['recording'])
        self.assertNotIn('Set-Cookie', headers)
        self.assertEqual(self.request('GET', '/api/v1/session')[0], 404)

    def test_compact_status_and_inventory_options(self):
        with patch.object(server, 'status_summary', return_value={'recording': {'state':'idle'}}) as summary, patch.object(server, 'status') as full:
            code, _, data = self.request('GET', '/api/v1/status')
            self.assertEqual(code, 200)
            self.assertEqual(json.loads(data)['recording']['state'], 'idle')
            summary.assert_called_once()
            full.assert_not_called()
        with patch.object(server, 'api_versions', return_value={'versions':{}}) as versions:
            self.assertEqual(self.request('GET', '/api/v1/versions?scope=all&details=true')[0], 200)
            versions.assert_called_once_with(None, details=True, scope='all')
        with patch.object(server, 'api_devices', return_value={'devices':{}}) as devices:
            self.assertEqual(self.request('GET', '/api/v1/devices?ids=cam_left')[0], 200)
            devices.assert_called_once_with(['cam_left'], details=False)
        self.assertEqual(self.request('GET', '/api/v1/status?details=1')[0], 400)

    def test_error_contract_and_task_identity(self):
        code, _, data = self.request('GET', '/api/v1/missing')
        self.assertEqual(code, 404)
        self.assertEqual(json.loads(data)['error'],
                         {'code':'not_found','message':'接口不存在','retryable':False})
        job = 'a'*32
        with patch('production_test.snapshot', return_value={'id':job,'status':'running'}):
            code, _, data = self.request('GET', '/api/v1/tasks/production/'+job)
            self.assertEqual(code, 200)
            self.assertEqual(json.loads(data)['id'], job)
            code, _, data = self.request('GET', '/api/v1/tasks/production/'+'b'*32)
            self.assertEqual(code, 404)
            self.assertEqual(json.loads(data)['error']['code'], 'task_not_found')
        with patch.object(server, 'rpc', return_value={'ok':True,'id':job}):
            code, _, data = self.request('POST', '/api/v1/snapshot',
                                        '{"kind":"local"}', {'X-UGripper-Request':'1'})
            self.assertEqual(code, 202)
            self.assertEqual(json.loads(data)['status_url'], '/api/v1/tasks/snapshot/'+job)

    def test_encoder_stream_has_explicit_terminal_message(self):
        def samples():
            yield {'left':{'status':'ok'},'right':{'status':'ok'}}
        with patch.object(server, 'stream_encoders', samples):
            code, _, data = self.request('POST', '/api/v1/encoder/stream', '{}',
                                         {'X-UGripper-Request':'1'})
        self.assertEqual(code, 200)
        messages = [json.loads(line) for line in data.splitlines()]
        self.assertEqual(messages[0]['type'], 'sample')
        self.assertEqual(messages[-1], {'type':'end','reason':'duration_limit'})
        def failed():
            yield {'left':{'status':'ok'}}
            raise ValueError('lost stream')
        with patch.object(server, 'stream_encoders', failed):
            _, _, data = self.request('POST', '/api/v1/encoder/stream', '{}',
                                      {'X-UGripper-Request':'1'})
        messages = [json.loads(line) for line in data.splitlines()]
        self.assertEqual(messages[-1]['error']['code'], 'encoder_stream_failed')
        self.assertEqual(messages[-1]['type'], 'error')

    def test_write_requires_origin_protection_and_operation_confirmation(self):
        body = json.dumps({'kind': 'zeroing'})
        self.assertEqual(self.request('POST', '/api/v1/execute', body)[0], 403)
        headers = {'X-UGripper-Request': '1', 'Host': 'device:8080', 'Origin': 'https://evil.example'}
        self.assertEqual(self.request('POST', '/api/v1/execute', body, headers)[0], 403)
        headers['Origin'] = 'https://device:8080'
        headers['X-UGripper-Maintenance-Password'] = 'daimon'
        with patch.object(server, 'rpc') as rpc:
            code, _, data = self.request('POST', '/api/v1/execute', body, headers)
        self.assertEqual(code, 400)
        self.assertIn('请确认维护操作', json.loads(data)['error']['message'])
        rpc.assert_not_called()

    def test_protected_routes_reject_missing_or_wrong_password_before_side_effects(self):
        for path in ('/api/v1/execute', '/api/v1/upload', '/api/v1/camera/version-confirm'):
            for password in (None, '', 'wrong', 'DAIMON', 'daimon '):
                with self.subTest(path=path, password=password):
                    headers = {'X-UGripper-Request': '1'}
                    if password is not None:
                        headers['X-UGripper-Maintenance-Password'] = password
                    with patch.object(server, 'rpc') as rpc, patch.object(server.Handler, 'upload') as upload:
                        code, _, data = self.request('POST', path, '{}', headers)
                    self.assertEqual(code, 403)
                    self.assertEqual(json.loads(data)['error']['message'], '维护密码错误，请重新输入')
                    rpc.assert_not_called()
                    upload.assert_not_called()

    def test_correct_password_dispatches_each_protected_operation_without_forwarding_secret(self):
        headers = {'X-UGripper-Request': '1', 'X-UGripper-Maintenance-Password': 'daimon'}
        for kind in ('configuration', 'zeroing', 'software', 'main_camera', 'gripper', 'rk3588_boot'):
            with self.subTest(kind=kind), patch.object(server, 'rpc', return_value={'id': 'demo'}) as rpc:
                code, _, _ = self.request('POST', '/api/v1/execute', json.dumps({'kind': kind, 'confirm': True, 'side': 'left'}), headers)
                self.assertEqual(code, 202)
                self.assertEqual(rpc.call_args.args[0]['kind'], kind)
                self.assertNotIn('daimon', json.dumps(rpc.call_args.args[0]))
        with patch.object(server.Handler, 'upload', lambda handler: handler.json({'upload': 'demo'}, 201)):
            self.assertEqual(self.request('POST', '/api/v1/upload', 'firmware', headers)[0], 201)

    def test_camera_version_decision_routes(self):
        payload = json.dumps({'id': 'job', 'token': 'token', 'accepted': True})
        headers = {'X-UGripper-Request': '1', 'X-UGripper-Maintenance-Password': 'daimon'}
        with patch.object(server, 'rpc', return_value={'ok': True}) as rpc:
            self.assertEqual(self.request('POST', '/api/v1/camera/version-confirm', payload, headers)[0], 200)
            rpc.assert_called_once_with({'action': 'camera_version_confirm', 'id': 'job',
                                         'token': 'token', 'accepted': True})
        # The cancel endpoint cannot be used to approve flashing, even with accepted=true.
        with patch.object(server, 'rpc', return_value={'ok': True}) as rpc:
            self.assertEqual(self.request('POST', '/api/v1/camera/version-cancel', payload,
                                          {'X-UGripper-Request': '1'})[0], 200)
            self.assertFalse(rpc.call_args.args[0]['accepted'])

    def test_nonprotected_operations_do_not_require_maintenance_password(self):
        headers = {'X-UGripper-Request': '1'}
        with patch.object(server, 'rpc', return_value={'status': 'ok'}) as rpc:
            code, _, _ = self.request('POST', '/api/v1/tactile/reset', json.dumps({'confirm': True, 'scope': 'both'}), headers)
            self.assertEqual(code, 200)
            self.assertEqual(rpc.call_args.args[0], {'action': 'tactile_reset', 'confirm': True, 'scope': 'both'})
        with patch.object(server, 'rpc', return_value={'id': 'demo'}) as rpc:
            self.assertEqual(self.request('POST', '/api/v1/snapshot', '{"kind":"local"}', headers)[0], 202)
            self.assertEqual(self.request('POST', '/api/v1/production/cancel', '{"id":"demo"}', headers)[0], 200)
            self.assertEqual(rpc.call_count, 2)
        with patch.object(server, 'read_encoders', return_value={}):
            self.assertEqual(self.request('POST', '/api/v1/encoder/read', '{}', headers)[0], 200)

    def test_password_does_not_bypass_origin_checks(self):
        headers = {'X-UGripper-Request': '1', 'X-UGripper-Maintenance-Password': 'daimon',
                   'Host': 'device:8080', 'Origin': 'https://evil.example'}
        with patch.object(server, 'rpc') as rpc:
            self.assertEqual(self.request('POST', '/api/v1/execute', '{"confirm":true}', headers)[0], 403)
            rpc.assert_not_called()

    def test_baseline_reset_still_requires_confirmation_and_origin_protection(self):
        with patch.object(server, 'rpc') as rpc:
            for confirm in (None, False, 'true'):
                code, _, _ = self.request('POST', '/api/v1/tactile/reset', json.dumps({'confirm': confirm}), {'X-UGripper-Request': '1'})
                self.assertEqual(code, 400)
            self.assertEqual(self.request('POST', '/api/v1/tactile/reset', '{"confirm":true}')[0], 403)
            headers = {'X-UGripper-Request': '1', 'Host': 'device:8080', 'Origin': 'https://evil.example'}
            self.assertEqual(self.request('POST', '/api/v1/tactile/reset', '{"confirm":true}', headers)[0], 403)
            rpc.assert_not_called()

    def test_public_device_api_routes_and_selectors(self):
        for path, function, key in [('/api/v1/versions?fields=ugripper,hardware_version', 'api_versions', 'versions'),
                                    ('/api/v1/devices?ids=cam_left,left_encoder', 'api_devices', 'devices')]:
            with patch.object(server,function,return_value={key:{}}) as read:
                code, _, body = self.request('GET',path)
                self.assertEqual(code,200)
                self.assertEqual(len(read.call_args.args[0]),2)
        with patch.object(server,'api_recording',return_value={'state':'recording','active':True}):
            code, _, body=self.request('GET','/api/v1/recording')
            self.assertEqual(code,200);self.assertTrue(json.loads(body)['active'])
        self.assertEqual(self.request('GET','/api/v1/versions?fields=')[0],400)

    def test_external_upgrade_is_immediate_and_does_not_forward_user_confirmation(self):
        headers={'X-UGripper-Request':'1','X-UGripper-Maintenance-Password':'daimon'}
        with patch.object(server,'rpc',return_value={'id':'a'*32}) as rpc:
            code, _, body=self.request('POST','/api/v1/upgrades',
                                       '{"kind":"main_camera","package":"camera.img","force":false}',headers)
            self.assertEqual(code,202)
            self.assertEqual(rpc.call_args.args[0],dict(action='external_upgrade',kind='main_camera',package='camera.img',sha256=None))
            self.assertEqual(self.request('POST','/api/v1/upgrades/'+'a'*32+'/reboot','{}',headers)[0],202)
            self.assertEqual(rpc.call_args.args[0],dict(action='upgrade_reboot',id='a'*32))
        for path in ('/api/v1/upgrades','/api/v1/upgrades/'+'a'*32+'/reboot'):
            with patch.object(server,'rpc') as rpc:
                self.assertEqual(self.request('POST',path,'{}',{'X-UGripper-Request':'1'})[0],403)
                rpc.assert_not_called()
        self.assertEqual(self.request('POST','/api/v1/upgrades','[]',headers)[0],400)

    def test_upgrade_result_found_and_missing(self):
        path='/api/v1/upgrades/'+'a'*32
        with patch.object(server,'task_result',return_value={'id':'a'*32,'status':'awaiting_reboot','success':True,'boot_verified':False}):
            code, _, body=self.request('GET',path)
            self.assertEqual(code,200);self.assertEqual(json.loads(body)['status'],'awaiting_reboot')
        with patch.object(server,'task_result',return_value={}):
            self.assertEqual(self.request('GET',path)[0],404)

    def test_setup_removes_legacy_password_and_preserves_certificate(self):
        script = Path(server.__file__).with_name('setup.py')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            etc = root / 'etc/ugripper-web'
            etc.mkdir(parents=True)
            (root / 'run/ugripper').mkdir(parents=True)
            settings = {'bind': '192.168.2.240', 'port': 8080, 'salt': 'old', 'password_hash': 'old'}
            (etc / 'config.json').write_text(json.dumps(settings))
            (etc / 'initial-password').write_text('obsolete')
            (etc / 'server.crt').write_text('existing certificate')
            (etc / 'server.key').write_text('existing key')
            with patch('pathlib.Path', side_effect=lambda p: root / p.lstrip('/')), patch('os.chown'), patch('subprocess.run') as command, patch('ssl.SSLContext') as tls, patch('pwd.getpwnam') as user:
                user.return_value.pw_uid = 1000
                user.return_value.pw_gid = 1000
                runpy.run_path(str(script))
                runpy.run_path(str(script))
            self.assertEqual(json.loads((etc / 'config.json').read_text()), {'bind': settings['bind'], 'port': 8080})
            self.assertFalse((etc / 'initial-password').exists())
            self.assertEqual((etc / 'server.crt').read_text(), 'existing certificate')
            command.assert_not_called()
            self.assertEqual(tls.return_value.load_cert_chain.call_count, 2)


if __name__ == '__main__':
    unittest.main()
