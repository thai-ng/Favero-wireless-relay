# Favero Épée Light Relay

Wireless relay for a Favero fencing scoring machine: a central unit at the
machine reads the Favero serial stream and forwards lamp hits over ESP-NOW
to battery-powered side units (one per fencer), which show the hit on an RGB
LED. One set = 1 central + 2 side units.

## Hardware

- **Central** (`central/`): classic ESP32 DevKit, USB-powered at the machine.
  Parses the Favero 10-byte frame (2400 baud 8N1) from USB Serial (emulator)
  or GPIO16 via a PC817 optocoupler (real Favero — interface perfboard,
  see `docs/`). Red LED GPIO25, green GPIO26, built-in blue GPIO2.
- **Side unit** (`side/`): Seeed XIAO ESP32-S3 (QFN56 rev v0.2, 8 MB PSRAM),
  socketed on perfboard with a 503450 LiPo (1000 mAh), SS12D00 slide
  switches, 12 mm tact button, RGB LED. **The kit flex antenna on the u.FL
  is required** — pairing fails without it.

## Pin map (side unit, by D-pad)

D-numbers are what the silkscreen shows; the XIAO S3 variant maps them
non-sequentially (D6=GPIO43, D7=GPIO44, D8=GPIO7, D9=GPIO8, D10=GPIO9).

| Function | Pad | GPIO |
|---|---|---|
| RGB red / green / blue | D1 / D4 / D3 | 2 / 5 / 4 (G/B legs soldered swapped, compensated in firmware) |
| Pairing button | D7 | 44 |
| Side switch | D9 | 8 |
| PWR switch (sleep signal) | D10 | 9 (RTC pin — wake source) |
| Battery | B+/B− pads | charger IC (SGM4056-ish, ~100 mA, red LED flashes while charging) |

## Protocol (v2)

ESP-NOW broadcast, Wi-Fi channel 6, box_id 1. Packets: LAMP / PAIR_BEACON /
PAIR_ACK / HELLO / TEST. Central heartbeat (LAMP) every 100 ms from boot,
burst ×3 on change. Side sends HELLO every 5 s when paired. Pairing: central
BOOT opens a 20 s window (beacons 10 Hz); side long-press ≥3 s learns only
beacons with RSSI ≥ −55 dBm seen 3×; pairing persists in NVS on both ends.
After pairing, the side accepts LAMP/TEST only from the saved central MAC.

Link monitoring (the "watchdog"): the side treats the link as up only while
packets arrive within 750 ms (`FAILSAFE_MS`); the central clears its lamp
state if no Favero frame arrives within 2 s. There is no hardware watchdog
timer — a wedged board is reset with the XIAO's onboard RST button.

## Power and sleep

The battery is wired straight to B+/B− so USB charging works while the board
is "off". The PWR switch is a sleep signal, not a power cut: LOW (knob
toward the grounded side) = deep sleep, HIGH = on. Wake: switch ON (ext1 —
GPIO9 is an RTC pin) or a 60 s timer tick. Auto-sleep: 15 min with no
central packets (paired only, never while learning/testing); a timer wake
with no central in 5 s sleeps again. The pairing button (GPIO44) cannot wake
from deep sleep — it is not an RTC pin.

Standby behaviour: deep sleep stops the radio entirely, so a sleeping side
unit cannot hear a hit. A hit wakes nothing — the next 60 s timer tick wakes
the board, it listens ~5 s for the central, and re-links if the central is
there. Consequence: a hit that lands while a unit is asleep is missed, and
the earliest it can show again is the next tick (up to ~60 s). The 15 min
idle timer is the trade-off knob: shorter saves battery, longer keeps the
unit responsive. Light sleep (radio on, CPU asleep) was not used: ESP-NOW
needs the radio awake to receive, so it saves little here.

## Flashing (side unit, ESP32-S3)

Bootloader lives at flash offset **0x0** on the S3, not 0x1000 (classic
ESP32). App-only flash preserves NVS/pairing:

```
esptool --chip esp32s3 --port COMx --baud 460800 write-flash \
  0x0 epee_side_xiao_s3.ino.bootloader.bin \
  0x8000 epee_side_xiao_s3.ino.partitions.bin \
  0x10000 epee_side_xiao_s3.ino.bin
```

or run `tools/flash_side_app_s3.ps1 -Port COMx` (Arduino-CLI build outputs).
Compile: `arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3`
(Arduino-ESP32 core 3.3.0).

## Tools (PC, Windows)

- `tools/emulator_web.py` — pretends to be the Favero: web UI at
  http://localhost:8765, streams genuine frames over the central's USB
  serial at 2400 baud. Hit buttons, score, auto-rearm (default 4 s).
- `tools/read_console.py` — reads the side unit console (auto-picks the
  non-COM3 port).
- `tools/log_pair.py` / `tools/log_side.py` — 100 s / 45 s console captures.
- `tools/verify_xiao_link.py`, `tools/verify_hits.ps1` — end-to-end link
  checks (fire hits, verify lamp logs).

## Build docs

`docs/PERFBOARD_SESSION.md` — receiver build order, power wiring, checks.
