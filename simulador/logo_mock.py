#!/usr/bin/env python3
"""Simulador experimental del protocolo web del LOGO! para laboratorio.
NO es un emulador de firmware Siemens. La codificacion AM/SETVARS es una hipotesis.
Solo para red local de laboratorio.
"""
import argparse
import json
import re
import secrets
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse


class State:
    def __init__(self, password, session_timeout=300, challenge_timeout=30, clock=time.monotonic):
        self.password = password
        self.lock = threading.Lock()
        self.pending = {}
        self.session = None
        self.session_expires = 0
        self.session_timeout = session_timeout
        self.challenge_timeout = challenge_timeout
        self.clock = clock
        self.data = {(20, i): 0 for i in range(8)}
        self.data.update({(131, i): 0 for i in range(32)})
        self.last_body = "(ninguna)"
        self.last_update = None
        self.last_panel = None


HTML = r'''<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CHQLogoWeb — laboratorio bidireccional</title><style>
:root{color-scheme:dark}body{font:16px system-ui;background:#101b26;color:#e3eef5;margin:2rem auto;padding:0 1rem;max-width:870px}
h1{font-size:1.6rem}.note{background:#243342;padding:1rem;border-left:4px solid #f5b94b}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(135px,1fr));gap:12px;margin-top:18px}
.card{border:1px solid #4a6070;border-radius:10px;padding:16px}.value{font-size:2rem;font-weight:bold;color:#92d5e7}
form{display:flex;gap:12px;align-items:center;flex-wrap:wrap;padding:16px;border:1px solid #4a6070;border-radius:10px}
input{padding:10px;width:140px;font-size:1rem;border:1px solid #7fa5b7;border-radius:6px;background:#122939;color:white}
button{padding:11px 18px;background:#74cae5;color:#0e2630;border:0;border-radius:6px;font-weight:bold;cursor:pointer}
label{font-weight:bold}code,pre{overflow-wrap:anywhere;color:#aedbdc}small{color:#a5bdc7}
</style></head>
<body><h1>CHQLogoWeb — Simulador V1 (mock)</h1>
<p class="note">Simulación de laboratorio. NO es un PLC Siemens y NO demuestra compatibilidad real. El formato de escritura analógica HTTP es una hipótesis pendiente de captura con hardware.</p>
<h2>Prueba bidireccional</h2>
<p><b>Web → ESP32:</b> introduce un valor en AM1. El ESP32 debe leerlo y copiarlo en AM2. <b>ESP32 → Web:</b> AM3 muestra una medición artificial que envía el ESP32-S3.</p>
<form id="setForm"><label for="am1">Escribir AM1</label><input id="am1" type="number" value="250" min="-32768" max="32767" step="1" required><button type="submit">Enviar a AM1</button><span id="result" role="status"></span></form>
<div id="cards" class="cards"></div><h3>Actividad de red</h3>
<pre id="last"></pre><small>Panel y simulador únicamente para una red privada de laboratorio.</small>
<script>
const $=id=>document.getElementById(id);
async function refresh(){try{const r=await fetch('/status',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);const s=await r.json();
$('cards').innerHTML=Object.entries(s.variables).map(([k,v])=>`<div class="card"><b>${k}</b><div class="value">${v}</div></div>`).join('');
$('last').textContent='Última operación HTTP del ESP32: '+s.last_body+'\nActualización: '+(s.last_update||'ninguna')+'\nÚltima escritura desde panel: '+(s.last_panel||'ninguna');
}catch(e){$('result').textContent='Error consultando el simulador: '+e.message}}
$('setForm').addEventListener('submit',async ev=>{ev.preventDefault();const value=Number($('am1').value);if(!Number.isInteger(value)||value< -32768||value>32767){$('result').textContent='Valor inválido';return}
try{const r=await fetch('/panel/am',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({am:1,value})});const data=await r.json();
if(!r.ok)throw Error(data.error||('HTTP '+r.status));$('result').textContent='AM1 actualizada a '+data.value;await refresh()}
catch(e){$('result').textContent='Error: '+e.message}});
refresh();setInterval(refresh,1000);
</script></body></html>'''


def handler_factory(state):
    class Handler(BaseHTTPRequestHandler):
        server_version = "LogoMock/1-lab"
        request_timeout = 5

        def setup(self):
            super().setup()
            self.connection.settimeout(self.request_timeout)

        def read_body(self, length):
            body = self.rfile.read(length)
            if len(body) != length:
                raise ValueError("cuerpo incompleto")
            return body

        def reply(self, status, data, content_type="text/plain; charset=utf-8"):
            if isinstance(data, str):
                data = data.encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = urlparse(self.path).path
            if path == "/":
                self.reply(200, HTML, "text/html; charset=utf-8")
                return
            if path == "/status":
                with state.lock:
                    variables = {f"AM{i+1}": state.data[(20,i)] for i in range(8)}
                    variables.update({f"M{i+1}": state.data[(131,i)] for i in range(4)})
                    payload = {"simulator": True, "variables": variables,
                               "last_body": state.last_body, "last_update": state.last_update,
                               "last_panel": state.last_panel}
                self.reply(200, json.dumps(payload), "application/json")
                return
            self.reply(404, "not found")

        def do_POST(self):
            path = urlparse(self.path).path
            if path == "/panel/am":
                # Control exclusivo del MOCK (no pertenece al protocolo Siemens).
                # Solo habilitado como herramienta de laboratorio en LAN confiable.
                origin = self.headers.get("Origin")
                expected_origin = f"http://{self.headers.get('Host', '')}"
                if origin is not None and origin != expected_origin:
                    self.reply(403, json.dumps({"error": "origen no permitido"}), "application/json")
                    return
                if self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
                    self.reply(415, json.dumps({"error": "se requiere application/json"}), "application/json")
                    return
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                    if not 1 <= length <= 256:
                        raise ValueError("longitud inválida")
                    obj = json.loads(self.read_body(length))
                    if (not isinstance(obj, dict) or set(obj) != {"am", "value"}
                            or type(obj["am"]) is not int or type(obj["value"]) is not int
                            or obj["am"] != 1 or not -32768 <= obj["value"] <= 32767):
                        raise ValueError("solo AM1 con entero de 16 bits")
                except TimeoutError:
                    self.reply(408, json.dumps({"error": "timeout de lectura"}), "application/json")
                    return
                except (ValueError, TypeError, json.JSONDecodeError) as ex:
                    self.reply(400, json.dumps({"error": str(ex)}), "application/json")
                    return
                with state.lock:
                    state.data[(20, 0)] = obj["value"]
                    state.last_panel = f"AM1={obj['value']} a las {time.strftime('%H:%M:%S')}"
                self.reply(200, json.dumps({"ok": True, "am": 1, "value": obj["value"]}), "application/json")
                return
            if path != "/AJAX":
                self.reply(404, "not found")
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length < 1 or length > 2048:
                    self.reply(413, "invalid body length")
                    return
                body = self.read_body(length).decode("utf-8").strip()
            except TimeoutError:
                self.reply(408, "body read timeout")
                return
            except (ValueError, UnicodeError):
                self.reply(400, "invalid body")
                return
            peer = self.client_address[0]
            hint = self.headers.get("Security-Hint", "")
            with state.lock:
                # El panel de estado nunca publica desafíos ni pruebas de contraseña.
                state.last_body = body.split(":", 1)[0] + ":[oculto]" if body.startswith(("UAMCHAL:", "UAMLOGIN:")) else body
                state.last_update = time.strftime("%Y-%m-%d %H:%M:%S")
                now = state.clock()
                state.pending = {ip: pending for ip, pending in state.pending.items() if pending[3] > now}
                if state.session_expires <= now:
                    state.session = None
                if body.startswith("UAMCHAL:") and hint == "p":
                    parts = body[8:].split(",")
                    try:
                        if len(parts) != 6 or parts[:2] != ["3", "4"]:
                            raise ValueError
                        if any(not re.fullmatch(r"[0-9]{1,10}", v) for v in parts[2:]):
                            raise ValueError
                        keys = [int(v) for v in parts[2:]]
                        if any(not 0 <= v <= 0xFFFFFFFF for v in keys):
                            raise ValueError
                    except ValueError:
                        self.reply(400, "invalid challenge")
                        return
                    temporary_hint = secrets.token_hex(16).upper()
                    challenge = secrets.randbits(32)
                    if peer not in state.pending and len(state.pending) >= 256:
                        self.reply(429, "too many pending challenges")
                        return
                    state.pending[peer] = (temporary_hint, challenge, keys, now + state.challenge_timeout)
                    self.reply(200, f"700,{temporary_hint},{challenge}")
                    return
                if body.startswith("UAMLOGIN:"):
                    pending = state.pending.pop(peer, None)
                    if not pending:
                        self.reply(403, "701,no challenge")
                        return
                    expected_hint, challenge, keys, _ = pending
                    parts = body[9:].split(",")
                    if len(parts) != 3 or parts[0] != "Web User" or hint != expected_hint:
                        self.reply(403, "701,invalid login")
                        return
                    token = (state.password + "+" + str(challenge)).encode("utf-8")[:32]
                    expected_pw = (zlib.crc32(token) ^ challenge) & 0xFFFFFFFF
                    expected_sc = challenge
                    for key in keys:
                        expected_sc ^= key
                    if (parts[1], parts[2]) != (str(expected_pw), str(expected_sc)):
                        self.reply(403, "701,bad password")
                        return
                    session_token = secrets.token_hex(16).upper()
                    state.session = (peer, session_token)
                    state.session_expires = now + state.session_timeout
                    self.reply(200, "700," + session_token)
                    return
                if state.session != (peer, hint):
                    self.reply(403, "701,invalid session")
                    return
                try:
                    cmd, rest = body.split(":", 1)
                    fields = rest.split(",")
                    if cmd == "GETVARS" and len(fields) == 6 and fields[0] == "v1":
                        _, kind, area, address, size, count = fields
                        if (area, size, count) != ("0", "1", "1"):
                            raise ValueError("unsupported length")
                        kind, address = int(kind), int(address)
                        value = state.data[(kind, address)]
                        rendered = f"{(value & 0xFFFF):04X}" if kind == 20 else f"{value:02X}"
                        self.reply(200, f"<rs><r i='v0' e='0' v='{rendered}'/></rs>", "application/xml")
                        return
                    if cmd == "SETVARS" and len(fields) == 7 and fields[0] == "v0":
                        _, kind, area, address, size, count, raw = fields
                        if (area, size, count) != ("0", "1", "1"):
                            raise ValueError("unsupported length")
                        kind, address = int(kind), int(address)
                        if (kind, address) not in state.data:
                            raise ValueError("unsupported address")
                        value = int(raw, 16)
                        if kind == 20:
                            if not re.fullmatch(r"[0-9A-Fa-f]{4}", raw) or not 0 <= value <= 0xFFFF:
                                raise ValueError("AM expects 4 hexadecimal digits (SIMULATION ONLY)")
                            value = value - 65536 if value >= 32768 else value
                        elif kind == 131:
                            if raw.upper() not in ("00", "01"):
                                raise ValueError("M expects 00 or 01")
                        else:
                            raise ValueError("unsupported variable type")
                        state.data[(kind, address)] = value
                        self.reply(200, f"<rs><r i='v0' e='0' v='{raw.upper()}'/></rs>", "application/xml")
                        return
                    raise ValueError("unsupported command")
                except (ValueError, KeyError) as ex:
                    self.reply(400, "mock protocol error: " + str(ex))

    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0", help="0.0.0.0 para acceso por LAN; 127.0.0.1 solo PC")
    parser.add_argument("--port", type=int, default=8088)
    parser.add_argument("--password", default="demo123", help="password SOLO del simulador de laboratorio")
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), handler_factory(State(args.password)))
    print(f"Simulador LOGO! (NO Siemens): http://127.0.0.1:{args.port}")
    print("Para ESP32, reemplaza 127.0.0.1 por la IP LAN de tu PC. Ctrl+C para detener.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
