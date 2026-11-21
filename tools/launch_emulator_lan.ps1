# Launch the Favero emulator web UI on the OptiPlex, reachable from
# another PC on the LAN at http://192.168.50.133:8765
# --host binds the LAN address (the GUI agent already holds 8765 on the
# Tailscale address, so 0.0.0.0:8765 would clash). --no-serial starts
# it unconnected; connect the central from the web UI (or with
# connect_central.ps1) once it is plugged in. Detached via CIM so it
# survives the SSH session.
# Topology 2026-11-20: central (CP2102) on COM5, XIAO side unit on
# COM4, built-in port COM1. The emulator serves the UI and streams
# frames to whichever COM port it is connected to.
$py = (Get-Command python).Source
$cmd = "`"$py`" `"C:\code\favero-epee\emulator_web.py`" --host 192.168.50.133 --no-serial"
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cmd; CurrentDirectory = 'C:\code\favero-epee' }
"create result: return=$($r.ReturnValue) pid=$($r.ProcessId)"
Start-Sleep 4
try { (Invoke-RestMethod http://192.168.50.133:8765/api/status) | ConvertTo-Json -Compress } catch { "HTTP fail: $($_.Exception.Message)" }
