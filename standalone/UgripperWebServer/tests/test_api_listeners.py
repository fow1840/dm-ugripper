import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import server


class PlainApiHandler(server.ApiHandler):
    def setup(self):
        BaseHTTPRequestHandler.setup(self)
    def log_message(self, *args):
        pass
    def log_request(self, *args):
        pass


class ApiListenersTests(unittest.TestCase):
    def test_exact_listener_allowlist(self):
        specs = server.listener_specs({})
        self.assertEqual([item[0] for item in specs],
                         [('192.168.2.240', 8080), ('192.168.2.240', 8081), ('127.0.0.1', 8081)])
        self.assertIs(specs[0][1], server.Handler)
        self.assertIs(specs[1][1], server.ApiHandler)
        for bind in ('0.0.0.0', '::', '', '10.30.120.65', 'localhost'):
            with self.subTest(bind=bind), self.assertRaises(ValueError):
                server.listener_specs({'bind': bind})
        for port in (8081, 0, 65536, True, '8080'):
            with self.subTest(port=port), self.assertRaises(ValueError):
                server.listener_specs({'port': port})

    def test_api_routes_without_static_assets_and_with_request_checks(self):
        with ThreadingHTTPServer(('127.0.0.1', 0), PlainApiHandler) as srv:
            thread = threading.Thread(target=srv.serve_forever)
            thread.start()
            def request(method, path, headers=None):
                conn = http.client.HTTPConnection(*srv.server_address, timeout=3)
                try:
                    conn.request(method, path, '{}' if method == 'POST' else None, headers or {})
                    response = conn.getresponse()
                    response.read()
                    return response.status
                finally:
                    conn.close()
            try:
                for path in ('/', '/app.js', '/style.css'):
                    self.assertEqual(request('GET', path), 404)
                with patch.object(server, 'status_summary', return_value={'ok': True}):
                    self.assertEqual(request('GET', '/api/v1/status'), 200)
                with patch.object(server, 'rpc', return_value={'ok': True, 'id': 'a'*32}) as rpc:
                    self.assertEqual(request('POST', '/api/v1/snapshot'), 403)
                    self.assertEqual(request('POST', '/api/v1/snapshot',
                        {'X-UGripper-Request': '1', 'Origin': 'https://other'}), 403)
                    rpc.assert_not_called()
                    self.assertEqual(request('POST', '/api/v1/snapshot',
                        {'X-UGripper-Request': '1'}), 202)
            finally:
                srv.shutdown()
                thread.join()


if __name__ == '__main__':
    unittest.main()
