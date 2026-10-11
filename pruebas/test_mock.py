import json
import threading
import unittest
import urllib.request
import urllib.error
import zlib
from http.server import ThreadingHTTPServer
from pathlib import Path
import importlib.util

script = Path(__file__).resolve().parents[1] / 'simulador' / 'logo_mock.py'
spec = importlib.util.spec_from_file_location('logo_mock', script)
mock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mock)

class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(('127.0.0.1', 0), mock.handler_factory(mock.State('demo123')))
        cls.port = cls.server.server_port
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def send(self, body, hint):
        req = urllib.request.Request(f'http://127.0.0.1:{self.port}/AJAX', body.encode(), method='POST',
                                     headers={'Security-Hint': hint})
        with urllib.request.urlopen(req) as resp:
            return resp.read().decode()

    def login(self):
        keys = [1,2,3,4]
        resp = self.send('UAMCHAL:3,4,1,2,3,4', 'p')
        status, hint, challenge = resp.split(',')
        self.assertEqual(status, '700')
        challenge = int(challenge)
        passwd = (zlib.crc32(f'demo123+{challenge}'.encode()) ^ challenge) & 0xFFFFFFFF
        proof = challenge
        for key in keys: proof ^= key
        result = self.send(f'UAMLOGIN:Web User,{passwd},{proof}', hint)
        status, token = result.split(',')
        self.assertEqual(status, '700')
        return token

    def test_auth_digital_and_analog(self):
        token = self.login()
        self.assertIn("e='0'", self.send('SETVARS:v0,20,0,0,1,1,0352', token))
        self.assertIn("v='0352'", self.send('GETVARS:v1,20,0,0,1,1', token))
        self.assertIn("e='0'", self.send('SETVARS:v0,131,0,16,1,1,01', token))
        self.assertIn("v='01'", self.send('GETVARS:v1,131,0,16,1,1', token))
        with urllib.request.urlopen(f'http://127.0.0.1:{self.port}/status') as resp:
            state = json.loads(resp.read())
        self.assertEqual(state['variables']['AM1'],850)

    def test_negative_analog(self):
        token = self.login()
        self.send('SETVARS:v0,20,0,1,1,1,FF9C', token)
        self.assertIn("v='FF9C'", self.send('GETVARS:v1,20,0,1,1,1', token))

    def panel(self, value):
        body = json.dumps({"am": 1, "value": value}).encode()
        req = urllib.request.Request(f'http://127.0.0.1:{self.port}/panel/am', body, method='POST',
                                     headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read())

    def test_bidirectional_web_am1_esp32_am2_am3(self):
        # Equivalente a navegador -> mock -> cliente HTTP del ESP32 -> mock.
        token = self.login()
        for n in (250, 500, -120):
            self.assertEqual(self.panel(n)['value'], n)
            raw = self.send('GETVARS:v1,20,0,0,1,1', token)
            self.assertIn(f"v='{(n & 0xffff):04X}'", raw)
            # Emula la función de eco del sketch.
            self.assertIn("e='0'", self.send(f'SETVARS:v0,20,0,1,1,1,{(n & 0xffff):04X}', token))
            with urllib.request.urlopen(f'http://127.0.0.1:{self.port}/status') as resp:
                result = json.loads(resp.read())
            self.assertEqual(result['variables']['AM1'], n)
            self.assertEqual(result['variables']['AM2'], n)
        self.assertIn("e='0'", self.send('SETVARS:v0,20,0,2,1,1,04B0', token))
        with urllib.request.urlopen(f'http://127.0.0.1:{self.port}/status') as resp:
            result = json.loads(resp.read())
        self.assertEqual(result['variables']['AM3'], 1200)
        self.assertIn('AM1=-120', result['last_panel'])

    def test_panel_rejects_invalid_value(self):
        for value in (32768, -32769, True, 1.5, '250'):
            payload = json.dumps({'am': 1, 'value': value}).encode()
            req = urllib.request.Request(f'http://127.0.0.1:{self.port}/panel/am', payload, method='POST',
                                         headers={'Content-Type':'application/json'})
            with self.assertRaises(urllib.error.HTTPError) as caught:
                urllib.request.urlopen(req)
            self.assertEqual(caught.exception.code, 400)

    def test_panel_rejects_cross_origin(self):
        payload = json.dumps({'am':1,'value':99}).encode()
        req = urllib.request.Request(f'http://127.0.0.1:{self.port}/panel/am', payload, method='POST',
                                     headers={'Content-Type':'application/json',
                                              'Origin':'https://example.org'})
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(req)
        self.assertEqual(caught.exception.code, 403)

    def test_no_auth_rejected(self):
        with self.assertRaises(urllib.error.HTTPError) as caught:
            self.send('SETVARS:v0,20,0,0,1,1,0352', 'incorrect')
        self.assertEqual(caught.exception.code,403)

if __name__ == '__main__': unittest.main()
