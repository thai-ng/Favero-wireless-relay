#!/usr/bin/env python3
"""Passive console listener - reads the side unit console WITHOUT
resetting it. Use while flipping the PWR switch: the switch-OFF line
("PWR switch OFF - deep sleep") prints once, then USB dies as the
chip sleeps. Lines are timestamped; a read failure (port vanishing)
is printed too, which itself confirms the sleep.

Usage: listen_console.py [seconds] [port]
  Default 300 s. Port auto-detects the XIAO ("USB Serial Device").
"""
import serial, sys, time
from serial.tools import list_ports

def pick_port():
    if len(sys.argv) > 2:
        return sys.argv[2]
    all_ports = list(list_ports.comports())
    print("ports:", [p.device for p in all_ports], flush=True)
    usb = [p.device for p in all_ports if "USB Serial Device" in (p.description or "")]
    cands = usb if len(usb) == 1 else [p.device for p in all_ports if p.device.upper() != "COM3"]
    if len(cands) != 1:
        print("AMBIGUOUS - candidates:", cands); sys.exit(1)
    return cands[0]

seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 300
port = pick_port()
print(f"listening on {port} for {seconds}s (no reset)", flush=True)
try:
    ser = serial.Serial(port, 115200, timeout=0.2)
except Exception as e:
    print(f"could not open {port}: {e}"); sys.exit(1)
# Lines to the safe "run app" state, WITHOUT the reset pulse that
# read_console.py does: DTR False = IO0 HIGH, RTS False = EN HIGH.
ser.setDTR(False)
ser.setRTS(False)
end = time.time() + seconds
buf = b""
try:
    while time.time() < end:
        try:
            data = ser.read(4096)
        except Exception as e:
            print(f"[{time.strftime('%H:%M:%S')}] READ FAIL (port gone?): {e}", flush=True)
            break
        if data:
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                print(f"[{time.strftime('%H:%M:%S')}] " + line.decode(errors="replace").rstrip("\r"), flush=True)
finally:
    try:
        ser.setDTR(False); ser.setRTS(False)
    except Exception:
        pass
    try:
        ser.close()
    except Exception:
        pass
print("---- end ----", flush=True)
