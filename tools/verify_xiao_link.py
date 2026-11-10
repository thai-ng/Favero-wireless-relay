import serial, sys, time, json, urllib.request
from serial.tools import list_ports

def pick_port():
    # Explicit port wins: `verify_xiao_link.py COM7` (laptop) / COM3 (OptiPlex).
    if len(sys.argv) > 1:
        return sys.argv[1]
    # Same auto-detect as read_console.py: prefer the XIAO ("USB Serial
    # Device"); fall back to the old non-COM3 rule.
    all_ports = list(list_ports.comports())
    print("ports:", [p.device for p in all_ports])
    usb = [p.device for p in all_ports if "USB Serial Device" in (p.description or "")]
    cands = usb if len(usb) == 1 else [p.device for p in all_ports if p.device.upper() != "COM3"]
    if len(cands) != 1:
        print("AMBIGUOUS - candidates:", cands); sys.exit(1)
    return cands[0]

def hit(side):
    req = urllib.request.Request(
        "http://localhost:8765/api/hit",
        data=json.dumps({"side": side}).encode(),
        headers={"Content-Type": "application/json"})
    urllib.request.urlopen(req, timeout=5).read()

ser = serial.Serial()
ser.port = pick_port()
print("using", ser.port)
ser.baudrate = 115200
ser.timeout = 0.2
ser.dtr = False
ser.rts = False
ser.open()
# app-boot reset: EN low 0.2 s (RTS True), release (RTS False), IO0 high
ser.setRTS(True); ser.setDTR(ser.dtr)
time.sleep(0.2)
ser.setRTS(False); ser.setDTR(ser.dtr)

lines = []
def collect(seconds):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        data = ser.read(4096)
        if data:
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                lines.append(line.decode(errors="replace").rstrip("\r"))

collect(4)
ready = [l for l in lines if "ready:" in l]
print("READY:", ready[-1] if ready else "NONE")

for side, code in (("left", "lamp=0x04"), ("right", "lamp=0x08")):
    before = len(lines)
    hit(side)
    collect(4)
    got = [l for l in lines[before:] if code in l]
    print(side.upper(), code, "->", "SEEN: " + got[0] if got else "NOT SEEN")
    collect(5)  # let the 4 s auto-rearm clear it before the next hit

print("---- full log ----")
for l in lines:
    print(l)
ser.setDTR(False); ser.setRTS(False)
time.sleep(0.1)
ser.close()
