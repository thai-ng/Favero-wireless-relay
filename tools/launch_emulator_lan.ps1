# Launch the Favero emulator web UI on the OptiPlex, reachable from
# another PC on the LAN at http://192.168.50.133:8765
# --host binds the LAN address (the GUI agent already holds 8765 on the
# Tailscale address, so 0.0.0.0:8765 would clash). --no-serial because
# the central is NOT on this machine (COM3 here is the side unit) -
# the UI serves, and you connect a central from the web UI if one is
# plugged in later. Detached via CIM so it survives the SSH session.
$py = (Get-Command python).Source
$cmd = "`"$py`" `"C:\code\favero-epee\emulator_web.py`" --host 192.168.50.133 --no-serial"
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cmd; CurrentDirectory = 'C:\code\favero-epee' }
"create result: return=$($r.ReturnValue) pid=$($r.ProcessId)"
Start-Sleep 4
try { (Invoke-RestMethod http://192.168.50.133:8765/api/status) | ConvertTo-Json -Compress } catch { "HTTP fail: $($_.Exception.Message)" }
