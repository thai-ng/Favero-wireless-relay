# End-to-end link verify: capture the side console, fire a left hit
# then a right hit through the emulator, dump the capture.
# EMU_URL: emulator web UI base. On the OptiPlex localhost:8765 is
# the GUI agent, so set $env:EMU_URL='http://192.168.50.133:8765'.
$emu = if ($env:EMU_URL) { $env:EMU_URL } else { "http://localhost:8765" }
Set-Location C:\code\favero-epee
powershell -ExecutionPolicy Bypass -File C:\code\favero-epee\launch_sidelogger.ps1 | Out-Null
Start-Sleep 2
Invoke-RestMethod -Uri "$emu/api/hit" -Method Post -ContentType 'application/json' -Body '{"side":"left"}' | Out-Null
Write-Output "left hit fired"
Start-Sleep 6
Invoke-RestMethod -Uri "$emu/api/hit" -Method Post -ContentType 'application/json' -Body '{"side":"right"}' | Out-Null
Write-Output "right hit fired"
Start-Sleep 40
Get-Content C:\code\favero-epee\sidelog.txt
