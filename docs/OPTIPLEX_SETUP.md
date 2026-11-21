# OptiPlex dev machine (set up 2026-11-09)

Second Favero side-unit dev machine, equivalent to the SL7 laptop.
SSH: `ssh -i ~/.ssh/id_ed25519 -o ProxyCommand="nc -X connect -x hatch-egress-proxy:3130 %h %p" thain@optiplex`
Windows, default shell cmd; use `powershell -Command` when needed.

## Toolchain (all verified 2026-11-09)
- arduino-cli 1.5.2-rc.1 at `C:\Tools\arduino-cli\arduino-cli.exe` (on Machine PATH)
- esp32 core 3.3.0 in `C:\Users\thain\AppData\Local\Arduino15`
- **No arduino-cli.yaml config file exists** — arduino-cli runs on
  defaults. Compile with no `--config-file`:
  `arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 --output-dir build_xiao_s3`
- Python 3.12.10, pyserial 3.5
- esptool v5.6.0 via pip (`esptool` on PATH). The standalone
  `tools\esptool-windows-amd64\` zip is NOT on this machine (the
  v5.6.0 GitHub asset URL 404s); `flash_side_app_s3.ps1` falls back
  to the pip `esptool` when the tools path is absent.

## Ports (2026-11-09)
- Side unit (XIAO ESP32S3, MAC 44:bd:8d:ed:4c:fc) = **COM3**
  ("USB Serial Device"). COM1 is the built-in port, ignore it.
- **Central now lives on the OptiPlex too** (decision 2026-11-20):
  plug the DevKit in and it enumerates as the CP210x device on
  the next free COM port (COM3 is the side, so likely COM4).
  Connect the emulator to it from the web UI
  (http://192.168.50.133:8765) or `POST /api/connect`.
  End-to-end link verify then runs from here.
- Verify scripts take the emulator URL from `EMU_URL`
  (default http://localhost:8765). On the OptiPlex set
  `$env:EMU_URL='http://192.168.50.133:8765'` — localhost:8765
  is the GUI agent. `verify_xiao_link.py` still auto-detects
  the side (the only "USB Serial Device"); `verify_link.py`
  needs an explicit port once the central is present (two
  hinted ports), e.g. `verify_link.py COM3`.
- Port scripts (`read_console.py`, `log_pair.py`, `log_side.py`,
  `verify_xiao_link.py`) pick the port by description "USB Serial
  Device" — works on both machines (explicit port arg also
  accepted, e.g. `verify_xiao_link.py COM7`). Note: the copy on
  the OptiPlex (`C:\code\favero-epee\`) still has the old COM6
  hardcode until it is re-synced from here.
- `flash_side_app_s3.ps1` defaults to `-Port COM7` (laptop):
  use `-Port COM3` on the OptiPlex.

## Emulator web UI on the LAN (2026-11-20)
- `launch_emulator_lan.ps1` (run with `-ExecutionPolicy Bypass`):
  starts `emulator_web.py --host 192.168.50.133 --no-serial`
  detached via CIM. Open from any PC on the LAN:
  **http://192.168.50.133:8765**
- Binds the LAN address because the GUI agent already holds 8765
  on the Tailscale address (100.121.140.75), so 0.0.0.0:8765
  would clash. Firewall rule "Favero emulator LAN" allows TCP
  8765 from 192.168.50.0/24.
- `--no-serial` because COM3 here is the side unit, not a
  central — the old `launch_cim.ps1` (COM3) would stream frames
  into the side unit's console port. With no central attached
  the UI shows connected:false; plug a central in and connect
  it from the web UI.
- `emulator_web.py` now takes `--host` / `--http-port` /
  `--no-serial` (positional serial port still accepted).
`C:\code\favero-epee\` — copy of `~/workspace/favero_relay/epee_bench/`
(source + scripts only, no build outputs). S3 flash offsets:
bootloader 0x0, partitions 0x8000, app 0x10000 (never 0x1000).

## Proof (2026-11-09)
Compiled 880,223 B, flashed app-only at 0x10000 to COM3 (hash
verified), console: `side unit (XIAO S3) ready: side=LEFT (switch),
paired to D4:E9:F4:EE:9A:F0 box 1`, link UP, state PAIRED.
