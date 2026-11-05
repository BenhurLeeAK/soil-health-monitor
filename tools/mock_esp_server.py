#!/usr/bin/env python3
"""Mock of the soil node API so the phone app can be built and tested without the ESP32.
Run:  python3 mock_esp_server.py [--port 8080] [--fail npk|zts|th]
Phone on the same Wi-Fi: use http://<this PC's IP>:8080 in the app's 'manual IP' field.
Android emulator: http://10.0.2.2:8080
Serves the exact JSON shape documented in docs/04-api-contract.md (schema 1)."""
import argparse, json, math, random, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

START = time.time()
ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, default=8080)
ap.add_argument("--fail", choices=["npk", "zts", "th"], help="simulate a sensor that does not answer")
ap.add_argument("--warmup", type=int, default=60, help="ZTS warm-up seconds (real device: 300)")
args = ap.parse_args()

def wave(base, amp, period, noise, dec):
    t = time.time() - START
    return round(base + amp * math.sin(2 * math.pi * t / period) + random.uniform(-noise, noise), dec)

def readings():
    up = int(time.time() - START)
    warm = max(0, args.warmup - up)
    ok_th, ok_npk, ok_zts = args.fail != "th", args.fail != "npk", args.fail != "zts"
    return {
        "schema": 1, "seq": up // 4, "uptime_s": up,
        "air": {"temp_c": wave(30, 3, 600, .2, 1) if ok_th else None,
                "rh_pct": wave(62, 8, 500, .5, 1) if ok_th else None, "ok": ok_th},
        "soil": {"moisture_pct": wave(28, 6, 420, .3, 1) if ok_zts else None,
                 "temp_c": wave(27, 2, 900, .1, 1) if ok_zts else None,
                 "ph": wave(6.5, .4, 700, .05, 1) if ok_zts else None,
                 "ok": ok_zts, "warming_up": warm > 0, "warmup_s_left": warm},
        "npk": {"n_mgkg": int(wave(55, 10, 800, 2, 0)) if ok_npk else None,
                "p_mgkg": int(wave(22, 5, 650, 1, 0)) if ok_npk else None,
                "k_mgkg": int(wave(120, 20, 900, 3, 0)) if ok_npk else None, "ok": ok_npk},
        "analog_moisture_pct": wave(30, 7, 430, .5, 1),
        "battery_v": wave(3.9, .05, 3000, .005, 2),
        "gps": None,
    }

class H(BaseHTTPRequestHandler):
    def _send(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        if self.path == "/api/info":
            self._send(200, {"device": "soil-node", "id": "MOCK", "fw": "mock", "schema": 1,
                             "ssid": "SoilNode-MOCK", "uptime_s": int(time.time() - START)})
        elif self.path == "/api/readings":
            self._send(200, readings())
        else:
            self._send(404, {"error": "not found"})
    def log_message(self, *a): pass

print(f"Mock soil node on port {args.port}  (fail={args.fail})")
ThreadingHTTPServer(("0.0.0.0", args.port), H).serve_forever()
