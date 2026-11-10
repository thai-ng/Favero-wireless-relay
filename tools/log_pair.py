import serial, time, sys

LOG = r"C:\code\favero-epee\pairlog.txt"
DUR = 100

from serial.tools import list_ports
all_ports = list(list_ports.comports())
usb = [p.device for p in all_ports if "USB Serial Device" in (p.description or "")]
cands = usb if len(usb) == 1 else [p.device for p in all_ports if p.device.upper() != "COM3"]
assert len(cands) == 1, f"candidates: {cands}"
ser = serial.Serial()
ser.port = cands[0]
ser.baudrate = 115200
ser.timeout = 0.2
ser.dtr = False   # IO0 HIGH = boot the app, not the ROM downloader
ser.rts = False   # EN HIGH = run
ser.open()

f = open(LOG, "w", buffering=1)
f.write("=== pair capture start ===\n")
end = time.time() + DUR
buf = b""
while time.time() < end:
    data = ser.read(4096)
    if data:
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            f.write("%.1f %s\n" % (time.time() % 10000,
                    line.decode(errors="replace").rstrip("\r")))
f.write("=== pair capture end ===\n")
f.close()
ser.setDTR(False)
ser.setRTS(False)
time.sleep(0.1)
ser.close()
