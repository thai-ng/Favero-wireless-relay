$py = (Get-Command python).Source
$cmd = "`"$py`" `"C:\code\favero-epee\emulator_web.py`" COM3"
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cmd; CurrentDirectory = 'C:\code\favero-epee' }
"create result: return=$($r.ReturnValue) pid=$($r.ProcessId)"
Start-Sleep 4
try { (Invoke-RestMethod http://localhost:8765/api/status) | ConvertTo-Json -Compress } catch { "HTTP fail: $($_.Exception.Message)" }
