#!/usr/bin/env python3
"""Favero epee emulator - web UI that feeds a real ESP32.

Serves a control page on http://localhost:8765 and streams genuine
Favero DATA-LINE frames (10 bytes, 2400 8N1, every 42 ms) out a
serial port to the ESP32 - the same bytes a real FA-05 would send.

    python emulator_web.py              # port auto: first COM found
    python emulator_web.py COM3         # force a port

Requires pyserial:  pip install pyserial

Epee only: the UI sets just the red (left, 0x04) and green (right,
0x08) lamp bits. White off-target bits are never sent.
"""

from __future__ import annotations

import json
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial missing - run:  pip install pyserial")
    raise SystemExit(1)

LAMP_RED = 0x04    # left hit
LAMP_GREEN = 0x08  # right hit
FRAME_PERIOD_S = 0.042
HTTP_PORT = 8765


def bcd(n: int) -> int:
    n = max(0, min(99, n))
    return ((n // 10) << 4) | (n % 10)


def build_frame(right=0, left=0, seconds=0, minutes=3, lamps=0,
                matches=0, cards=0) -> bytes:
    body = bytes([0xFF, bcd(right), bcd(left), bcd(seconds),
                  bcd(minutes), lamps & 0x3F, matches & 0xFF, 0x00,
                  cards & 0xFF])
    return body + bytes([sum(body) & 0xFF])


class Emulator:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.left_score = 0
        self.right_score = 0
        self.minutes = 3
        self.seconds = 0
        self.lamps = 0
        # Automatic rearm, like the real box (REARM. AUTOM. on the
        # Full-Arm series): lamps clear by themselves this many
        # seconds after the last hit. 0 = manual only. The machine's
        # own choices are 2/4/6/120 s; default 4 s.
        self.rearm_s = 4.0
        self._lamp_deadline = None
        self.ser = None
        self.port = None
        self.frames_sent = 0
        self.last_frame = b""
        self._stop = threading.Event()
        self._demo_thread = None

    def _arm_rearm_locked(self) -> None:
        """(Re)start the auto-clear timer. Call with lock held."""
        if self.rearm_s > 0 and self.lamps:
            self._lamp_deadline = time.monotonic() + self.rearm_s
        else:
            self._lamp_deadline = None

    def _check_rearm(self) -> None:
        with self.lock:
            if (self._lamp_deadline is not None
                    and time.monotonic() >= self._lamp_deadline):
                self.lamps = 0
                self._lamp_deadline = None

    # -- serial ----------------------------------------------------
    def connect(self, port: str) -> None:
        self.disconnect()
        self.ser = serial.Serial(port, 2400, timeout=0.5)
        self.port = port
        # DevKit auto-reset on open is fine: ESP32 boots in ~1 s and
        # the streamer runs continuously, the parser re-locks itself.

    def disconnect(self) -> None:
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self.port = None

    def frame(self) -> bytes:
        with self.lock:
            return build_frame(right=self.right_score,
                               left=self.left_score,
                               seconds=self.seconds,
                               minutes=self.minutes,
                               lamps=self.lamps)

    def stream_loop(self) -> None:
        while not self._stop.is_set():
            self._check_rearm()
            if self.ser is not None:
                try:
                    data = self.frame()
                    self.ser.write(data)
                    self.last_frame = data
                    self.frames_sent += 1
                except Exception:
                    self.disconnect()
            self._stop.wait(FRAME_PERIOD_S)

    # -- actions ---------------------------------------------------
    def hit(self, side: str) -> None:
        with self.lock:
            if side in ("left", "both"):
                self.lamps |= LAMP_RED
                self.left_score = min(15, self.left_score + 1)
            if side in ("right", "both"):
                self.lamps |= LAMP_GREEN
                self.right_score = min(15, self.right_score + 1)
            self._arm_rearm_locked()

    def lamps_off(self) -> None:
        with self.lock:
            self.lamps = 0
            self._lamp_deadline = None

    def set_lamps(self, lamps: int) -> None:
        with self.lock:
            self.lamps = lamps & (LAMP_RED | LAMP_GREEN)
            if self.lamps:
                self._arm_rearm_locked()
            else:
                self._lamp_deadline = None

    def set_rearm(self, seconds: float) -> None:
        with self.lock:
            self.rearm_s = max(0.0, float(seconds))
            self._arm_rearm_locked()

    def adjust(self, field: str, delta: int) -> None:
        with self.lock:
            if field == "left":
                self.left_score = max(0, min(99, self.left_score + delta))
            elif field == "right":
                self.right_score = max(0, min(99, self.right_score + delta))
            elif field == "minutes":
                self.minutes = max(0, min(99, self.minutes + delta))
            elif field == "seconds":
                self.seconds = max(0, min(59, self.seconds + delta))

    def reset_all(self) -> None:
        with self.lock:
            self.left_score = 0
            self.right_score = 0
            self.lamps = 0
            self._lamp_deadline = None
            self.minutes = 3
            self.seconds = 0

    def demo(self) -> None:
        if self._demo_thread and self._demo_thread.is_alive():
            return

        def run():
            steps = [(LAMP_RED, "left"), (0, "off"), (LAMP_GREEN, "right"),
                     (0, "off"), (LAMP_RED | LAMP_GREEN, "both"), (0, "off")]
            for lamps, _ in steps:
                self.set_lamps(lamps)
                time.sleep(1.4)
        self._demo_thread = threading.Thread(target=run, daemon=True)
        self._demo_thread.start()

    _ports_cache = []
    _ports_cache_ms = 0.0

    def _ports(self):
        # list_ports.comports() takes seconds on Windows - and the UI
        # polls status 3x/s - so cache the enumeration for 10 s.
        now = time.monotonic()
        if now - Emulator._ports_cache_ms > 10:
            Emulator._ports_cache = [p.device
                                      for p in list_ports.comports()]
            Emulator._ports_cache_ms = now
        return Emulator._ports_cache

    def status(self) -> dict:
        with self.lock:
            return {
                "connected": self.ser is not None,
                "port": self.port,
                "ports": self._ports(),
                "left_score": self.left_score,
                "right_score": self.right_score,
                "minutes": self.minutes,
                "seconds": self.seconds,
                "lamps": self.lamps,
                "rearm_s": self.rearm_s,
                "red": bool(self.lamps & LAMP_RED),
                "green": bool(self.lamps & LAMP_GREEN),
                "frames_sent": self.frames_sent,
                "frame_hex": self.last_frame.hex(" "),
            }


EMU = Emulator()

PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>Favero Epee Emulator</title>
<style>
 body{background:#111;color:#eee;font-family:system-ui;margin:24px}
 .row{display:flex;gap:16px;flex-wrap:wrap;align-items:stretch}
 .lamp{flex:1;min-width:220px;border-radius:14px;padding:28px;text-align:center;
       font-size:28px;font-weight:700;background:#2a2a2a;transition:background .1s}
 .lamp.red.on{background:#d21f26}.lamp.green.on{background:#1db954}
 .score{font-size:56px}
 button{font-size:17px;padding:12px 18px;border-radius:10px;border:0;
        background:#333;color:#eee;cursor:pointer}
 button:hover{background:#444}
 button.hit-red{background:#7a1216}button.hit-green{background:#0f6b32}
 .panel{background:#1b1b1b;border-radius:12px;padding:16px;margin-top:16px}
 small{color:#aaa} select,input{background:#222;color:#eee;border:1px solid #444;
 border-radius:6px;padding:8px;font-size:15px}
</style></head><body>
<h2>Favero epee emulator &rarr; ESP32</h2>
<div class="row">
  <div class="lamp red" id="lampL">LEFT<div class="score" id="scoreL">0</div></div>
  <div class="lamp green" id="lampR">RIGHT<div class="score" id="scoreR">0</div></div>
</div>
<div class="panel row">
  <button class="hit-red" onclick="hit('left')">Left hit</button>
  <button class="hit-green" onclick="hit('right')">Right hit</button>
  <button onclick="hit('both')">Double touch</button>
  <button onclick="api('/api/lamps_off')">Lamps off (manual rearm)</button>
  <button onclick="api('/api/demo')">Demo sequence</button>
  &nbsp; Auto-clear (rearm):
  <select id="rearm" onchange="api('/api/rearm',{seconds:parseFloat(this.value)})">
    <option value="0">manual only</option>
    <option value="2">2 s</option>
    <option value="4" selected>4 s</option>
    <option value="6">6 s</option>
    <option value="120">120 s</option>
  </select>
</div>
<div class="panel">
  Score: left <button onclick="adj('left',1)">+</button>
  <button onclick="adj('left',-1)">&minus;</button>
  right <button onclick="adj('right',1)">+</button>
  <button onclick="adj('right',-1)">&minus;</button>
  &nbsp; Clock <span id="clock">3:00</span>
  <button onclick="adj('minutes',1)">min+</button>
  <button onclick="adj('seconds',10)">sec+10</button>
  &nbsp; <button onclick="api('/api/reset_all')">Reset all</button>
</div>
<div class="panel">
  Serial port <select id="port"></select>
  <button onclick="connect()">Connect</button>
  <button onclick="api('/api/disconnect').then(refresh)">Disconnect</button>
  <div><small id="status">...</small></div>
  <div><small>frame: <span id="frame"></span></small></div>
</div>
<script>
async function api(path, body){
  const r = await fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},
    body: JSON.stringify(body||{})});
  return r.json();
}
async function hit(s){ await api('/api/hit',{side:s}); refresh(); }
async function adj(f,d){ await api('/api/adjust',{field:f,delta:d}); refresh(); }
async function connect(){ await api('/api/connect',{port:document.getElementById('port').value}); refresh(); }
async function refresh(){
  const s = await (await fetch('/api/status')).json();
  document.getElementById('lampL').className = 'lamp red' + (s.red?' on':'');
  document.getElementById('lampR').className = 'lamp green' + (s.green?' on':'');
  document.getElementById('scoreL').textContent = s.left_score;
  document.getElementById('scoreR').textContent = s.right_score;
  document.getElementById('clock').textContent = s.minutes + ':' + String(s.seconds).padStart(2,'0');
  document.getElementById('frame').textContent = s.frame_hex;
  document.getElementById('status').textContent =
    (s.connected ? 'streaming to '+s.port : 'NOT CONNECTED') +
    ' - frames sent: ' + s.frames_sent + ' - 2400 8N1, 42 ms cadence';
  const rs = document.getElementById('rearm');
  if (document.activeElement !== rs) rs.value = String(s.rearm_s);
  const sel = document.getElementById('port');
  if (document.activeElement !== sel){
    const cur = sel.value;
    sel.innerHTML = s.ports.map(p=>'<option'+(p===s.port?' selected':'')+'>'+p+'</option>').join('');
    if (cur && s.ports.includes(cur)) sel.value = cur;
  }
}
setInterval(refresh, 300); refresh();
</script></body></html>"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):  # keep console quiet
        pass

    def _json(self, obj, code=200):
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/":
            data = PAGE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        elif self.path == "/api/status":
            self._json(EMU.status())
        else:
            self._json({"error": "not found"}, 404)

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        try:
            body = json.loads(self.rfile.read(n) or b"{}")
        except Exception:
            body = {}
        try:
            if self.path == "/api/hit":
                EMU.hit(body.get("side", ""))
            elif self.path == "/api/lamps_off":
                EMU.lamps_off()
            elif self.path == "/api/adjust":
                EMU.adjust(body.get("field", ""), int(body.get("delta", 0)))
            elif self.path == "/api/reset_all":
                EMU.reset_all()
            elif self.path == "/api/rearm":
                EMU.set_rearm(float(body.get("seconds", 4)))
            elif self.path == "/api/demo":
                EMU.demo()
            elif self.path == "/api/connect":
                EMU.connect(body.get("port") or EMU.status()["ports"][0])
            elif self.path == "/api/disconnect":
                EMU.disconnect()
            else:
                return self._json({"error": "not found"}, 404)
            self._json({"ok": True, **EMU.status()})
        except Exception as e:
            self._json({"error": str(e)}, 500)


def main() -> int:
    threading.Thread(target=EMU.stream_loop, daemon=True).start()
    wanted = sys.argv[1] if len(sys.argv) > 1 else None
    ports = [p.device for p in list_ports.comports()]
    target = wanted or (ports[0] if ports else None)
    if target:
        try:
            EMU.connect(target)
            print(f"streaming Favero frames on {target} @2400 8N1")
        except Exception as e:
            print(f"could not open {target}: {e} - use the web UI to connect")
    else:
        print("no serial port found - connect from the web UI")
    print(f"open http://localhost:{HTTP_PORT}")
    ThreadingHTTPServer(("127.0.0.1", HTTP_PORT), Handler).serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
