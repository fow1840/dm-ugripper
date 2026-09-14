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
        self.assertNotIn(b'type="password"', body)
        self.assertNotIn('Set-Cookie', headers)
        with patch.object(server, 'status', return_value={'recording': False}):
            code, headers, body = self.request('GET', '/api/v1/status')
        self.assertEqual(code, 200)
        self.assertFalse(json.loads(body)['recording'])
        self.assertNotIn('Set-Cookie', headers)
        self.assertEqual(self.request('GET', '/api/v1/session')[0], 404)

    def test_write_requires_origin_protection_and_operation_confirmation(self):
        body = json.dumps({'kind': 'zeroing'})
        self.assertEqual(self.request('POST', '/api/v1/execute', body)[0], 403)
        headers = {'X-UGripper-Request': '1', 'Host': 'device:8080', 'Origin': 'https://evil.example'}
        self.assertEqual(self.request('POST', '/api/v1/execute', body, headers)[0], 403)
        headers['Origin'] = 'https://device:8080'
        with patch.object(server, 'rpc') as rpc:
            code, _, data = self.request('POST', '/api/v1/execute', body, headers)
        self.assertEqual(code, 400)
        self.assertIn('请确认维护操作', json.loads(data)['error'])
        rpc.assert_not_called()

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
            with patch('pathlib.Path', side_effect=lambda p: root / p.lstrip('/')), patch('os.chown'), patch('subprocess.run') as command, patch('pwd.getpwnam') as user:
                user.return_value.pw_uid = 1000
                user.return_value.pw_gid = 1000
                runpy.run_path(str(script))
                runpy.run_path(str(script))
            self.assertEqual(json.loads((etc / 'config.json').read_text()), {'bind': settings['bind'], 'port': 8080})
            self.assertFalse((etc / 'initial-password').exists())
            self.assertEqual((etc / 'server.crt').read_text(), 'existing certificate')
            command.assert_not_called()


if __name__ == '__main__':
    unittest.main()
