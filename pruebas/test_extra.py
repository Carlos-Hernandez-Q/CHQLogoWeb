"""Casos extra de regresión para el mock HTTP (no validación de firmware LOGO!)."""
import importlib.util
import json
import socket
from pathlib import Path
import threading
import unittest
import urllib.error
import urllib.request
import zlib
from http.server import ThreadingHTTPServer

FILE = Path(__file__).resolve().parents[1] / "simulador" / "logo_mock.py"
spec = importlib.util.spec_from_file_location("logo_mock_extra", FILE)
mock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mock)


class ExtraTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.state = mock.State("demo123")
        handler = mock.handler_factory(cls.state)
        handler.request_timeout = 0.2
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.thread.join()
        cls.server.server_close()

    def send(self, command, hint):
        req = urllib.request.Request(self.url + "/AJAX", command.encode(), method="POST",
                                     headers={"Security-Hint": hint})
        with urllib.request.urlopen(req) as r:
            return r.read().decode()

    def login(self, password="demo123"):
        status, hint, challenge = self.send("UAMCHAL:3,4,1,2,3,4", "p").split(",")
        self.assertEqual(status, "700")
        challenge = int(challenge)
        pw = (zlib.crc32((password + "+" + str(challenge)).encode()[:32]) ^ challenge) & 0xFFFFFFFF
        proof = 1 ^ 2 ^ 3 ^ 4 ^ challenge
        result = self.send(f"UAMLOGIN:Web User,{pw},{proof}", hint)
        self.assertTrue(result.startswith("700,"))
        return result.split(",")[1]

    def test_bounds_analog(self):
        token = self.login()
        for index, signed, raw in ((0, 32767, "7FFF"), (1, -32768, "8000"), (7, 0, "0000")):
            self.assertIn("e='0'", self.send(f"SETVARS:v0,20,0,{index},1,1,{raw}", token))
            self.assertIn(f"v='{raw}'", self.send(f"GETVARS:v1,20,0,{index},1,1", token))
            with urllib.request.urlopen(self.url + "/status") as response:
                result = json.load(response)
            self.assertEqual(result["variables"][f"AM{index+1}"], signed)

    def test_digital_read_write_both_states(self):
        token = self.login()
        for number in ("00", "01", "00"):
            self.send(f"SETVARS:v0,131,0,0,1,1,{number}", token)
            self.assertIn(f"v='{number}'", self.send("GETVARS:v1,131,0,0,1,1", token))

    def test_reject_bad_hex_or_address(self):
        token = self.login()
        for command in (
            "SETVARS:v0,20,0,0,1,1,ZZZZ",
            "SETVARS:v0,20,0,0,1,1,FFFFF",
            "SETVARS:v0,20,0,0,1,1,+001",
            "SETVARS:v0,20,0,0,1,1,0xFF",
            "SETVARS:v0,20,0,0,1,1,1_00",
            "SETVARS:v0,20,0,8,1,1,00FA",
            "SETVARS:v0,131,0,0,1,1,03",
            "GETVARS:v1,20,0,8,1,1",
        ):
            with self.subTest(command=command):
                with self.assertRaises(urllib.error.HTTPError) as caught:
                    self.send(command, token)
                self.assertEqual(caught.exception.code, 400)

    def test_login_bad_password(self):
        hint_text = self.send("UAMCHAL:3,4,1,2,3,4", "p")
        _, temporary_hint, challenge_text = hint_text.split(",")
        challenge = int(challenge_text)
        pw = (zlib.crc32(f"incorrecta+{challenge}".encode()) ^ challenge) & 0xFFFFFFFF
        proof = 1 ^ 2 ^ 3 ^ 4 ^ challenge
        with self.assertRaises(urllib.error.HTTPError) as caught:
            self.send(f"UAMLOGIN:Web User,{pw},{proof}", temporary_hint)
        self.assertEqual(caught.exception.code, 403)

    def test_session_replacement(self):
        first = self.login()
        second = self.login()
        self.assertNotEqual(first, second)
        with self.assertRaises(urllib.error.HTTPError) as caught:
            self.send("GETVARS:v1,20,0,0,1,1", first)
        self.assertEqual(caught.exception.code, 403)
        self.assertIn("e='0'", self.send("GETVARS:v1,20,0,0,1,1", second))

    def test_status_redacts_authentication_proofs(self):
        self.login()
        with urllib.request.urlopen(self.url + "/status") as response:
            result = json.load(response)
        self.assertEqual(result["last_body"], "UAMLOGIN:[oculto]")

    def test_session_expiry_and_reauthentication(self):
        old_clock = self.state.clock
        clock = [old_clock()]
        self.state.clock = lambda: clock[0]
        try:
            token = self.login()
            clock[0] += self.state.session_timeout + 1
            with self.assertRaises(urllib.error.HTTPError) as caught:
                self.send("GETVARS:v1,20,0,0,1,1", token)
            self.assertEqual(caught.exception.code, 403)
            token = self.login()
            self.assertIn("e='0'", self.send("GETVARS:v1,20,0,0,1,1", token))
        finally:
            self.state.clock = old_clock
            self.state.session = None
            self.state.pending.clear()

    def test_expired_challenge_rejected(self):
        old_clock = self.state.clock
        clock = [old_clock()]
        self.state.clock = lambda: clock[0]
        try:
            _, hint, _ = self.send("UAMCHAL:3,4,1,2,3,4", "p").split(",")
            clock[0] += self.state.challenge_timeout + 1
            with self.assertRaises(urllib.error.HTTPError) as caught:
                self.send("UAMLOGIN:Web User,0,0", hint)
            self.assertEqual(caught.exception.code, 403)
        finally:
            self.state.clock = old_clock
            self.state.pending.clear()

    def test_incomplete_request_times_out(self):
        with socket.create_connection(("127.0.0.1", self.server.server_port), timeout=2) as client:
            client.sendall(b"POST /AJAX HTTP/1.0\r\nContent-Length: 20\r\n\r\nx")
            response = client.recv(1024)
        self.assertIn(b"408", response.split(b"\r\n", 1)[0])


if __name__ == "__main__":
    unittest.main()
