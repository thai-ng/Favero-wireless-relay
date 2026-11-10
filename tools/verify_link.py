#!/usr/bin/env python3
"""End-to-end link check, run ON the PC.

Opens the side unit's console (115200), fires hits through the
emulator's web API, and prints what the side unit logged - proving
PC -> central (USB) -> ESP-NOW -> side unit works without eyes.
Opening the port resets the side unit; that's fine, it re-links itself.

Port selection: pass it explicitly (`verify_link.py COM7` or
`--port COM7`) or let it auto-detect: a single USB-serial port whose
description mentions XIAO / ESP32 / USB Serial / JTAG wins; if exactly
one COM port exists at all it is used; otherwise the candidates are
listed and the script exits asking for an explicit port. (The old
COM4 default was the DevKit's port — the XIAO enumerates differently.)
"""
import argparse
import json
import sys
import threading
import time
import urllib.request

import serial
from serial.tools import list_ports

lines = []


def pick_port(explicit):
    if explicit:
        return explicit
    ports = list(list_ports.comports())
    hints = ("xiao", "esp32", "usb serial", "jtag", "cp210", "ch340", "ch910")
    hinted = [p for p in ports
              if any(h in (p.description or "").lower() for h in hints)]
    if len(hinted) == 1:
        return hinted[0].device
    if len(ports) == 1:
        return ports[0].device
    if not ports:
        sys.exit("No COM ports found — is the side unit plugged in?")
    listing = "\n".join(f"  {p.device}: {p.description}" for p in ports)
    sys.exit("Multiple candidate ports; re-run with one explicitly, "
             f"e.g. verify_link.py {ports[0].device}\n{listing}")


def post(path, body):
    req = urllib.request.Request(
        "http://localhost:8765" + path,
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"}, method="POST")
    urllib.request.urlopen(req, timeout=5).read()


def reader(ser):
    while True:
        try:
            line = ser.readline().decode(errors="replace").strip()
        except Exception:
            return
        if line:
            lines.append(f"{time.strftime('%H:%M:%S')} {line}")
            print(lines[-1], flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("port", nargs="?", help="side-unit COM port, e.g. COM7")
    ap.add_argument("--port", dest="port_opt", help="same, as an option")
    args = ap.parse_args()
    port = pick_port(args.port_opt or args.port)
    print(f"Opening side-unit console on {port} (115200)", flush=True)
    ser = serial.Serial(port, 115200, timeout=0.5)
    threading.Thread(target=reader, args=(ser,), daemon=True).start()
    time.sleep(3)                      # boot + lamp test + ESP-NOW up
    post("/api/rearm", {"seconds": 2})
    post("/api/reset_all", {})
    time.sleep(0.5)
    print(">>> HIT LEFT", flush=True)
    post("/api/hit", {"side": "left"})
    time.sleep(2.5)                    # includes the 2 s auto-clear
    print(">>> HIT RIGHT", flush=True)
    post("/api/hit", {"side": "right"})
    time.sleep(1.5)
    post("/api/lamps_off", {})
    post("/api/rearm", {"seconds": 4})
    post("/api/reset_all", {})
    time.sleep(0.5)
    text = "\n".join(lines)
    ok_left = "left_hit=1" in text
    ok_right = "right_hit=1" in text
    print(f"RESULT: left_seen={ok_left} right_seen={ok_right} -> "
          f"{'LINK PASS' if ok_left and ok_right else 'LINK FAIL'}")


if __name__ == "__main__":
    main()
