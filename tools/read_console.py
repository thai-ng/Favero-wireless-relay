import serial, sys, time
from serial.tools import list_ports

all_ports = list(list_ports.comports())
print("ports:", [p.device for p in all_ports])
# Prefer the XIAO ("USB Serial Device" - its native-USB description on
# both machines); fall back to the old non-COM3 rule. On the OptiPlex
# the side unit IS COM3, so the old rule would pick COM1 by mistake.
usb = [p.device for p in all_ports if "USB Serial Device" in (p.description or "")]
cands = usb if len(usb) == 1 else [p.device for p in all_ports if p.device.upper() != "COM3"]
if len(cands) != 1:
    print("AMBIGUOUS - candidates:", cands); sys.exit(1)
port = cands[0]
print("reading", port)
ser = serial.Serial(port, 115200, timeout=0.2)
time.sleep(0.3)
# Boot the APP (not the ROM downloader): on USB-Serial/JTAG the line
# levels are inverted vs pin names - DTR False = IO0 HIGH (normal
# boot), RTS True = EN LOW (reset), RTS False = run. (esp_pylib
# serial_reset: PIN_LOW=True, PIN_HIGH=False.)
ser.setDTR(False)
ser.setRTS(False)
time.sleep(0.2)
ser.setRTS(True)   # EN low: reset
ser.setDTR(ser.dtr)
time.sleep(0.2)
ser.setRTS(False)  # EN high: boot app
ser.setDTR(ser.dtr)
end = time.time() + 12
buf = b""
while time.time() < end:
    data = ser.read(4096)
    if data:
        buf += data
ser.setDTR(False)
ser.setRTS(False)
time.sleep(0.1)
ser.close()
print("---- console ----")
print(buf.decode(errors="replace"))
print("---- end ----")
