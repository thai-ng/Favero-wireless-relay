$py = (Get-Command python).Source
$cmd = "`"$py`" `"C:\code\favero-epee\log_pair.py`""
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cmd; CurrentDirectory = 'C:\code\favero-epee' }
"logger: return=$($r.ReturnValue) pid=$($r.ProcessId)"
Start-Sleep 3
Get-Content C:\code\favero-epee\pairlog.txt -ErrorAction SilentlyContinue
