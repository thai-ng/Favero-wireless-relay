# End-to-end link verify: capture the side console, fire a left hit
# then a right hit through the emulator, dump the capture.
Set-Location C:\code\favero-epee
powershell -ExecutionPolicy Bypass -File C:\code\favero-epee\launch_sidelogger.ps1 | Out-Null
Start-Sleep 2
Invoke-RestMethod -Uri http://localhost:8765/api/hit -Method Post -ContentType 'application/json' -Body '{"side":"left"}' | Out-Null
Write-Output "left hit fired"
Start-Sleep 6
Invoke-RestMethod -Uri http://localhost:8765/api/hit -Method Post -ContentType 'application/json' -Body '{"side":"right"}' | Out-Null
Write-Output "right hit fired"
Start-Sleep 40
Get-Content C:\code\favero-epee\sidelog.txt
