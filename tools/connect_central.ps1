# Connect the OptiPlex emulator (http://192.168.50.133:8765) to the
# central unit's COM port. The emulator is a plain HTTP server, so
# this runs on the OptiPlex itself. Usage:
#   powershell -ExecutionPolicy Bypass -File C:\code\favero-epee\connect_central.ps1 -Port COM5
param([string]$Port = "COM5")
$ErrorActionPreference = "Stop"
$base = "http://192.168.50.133:8765"
$body = @{ port = $Port } | ConvertTo-Json
Invoke-RestMethod -Method Post -Uri "$base/api/connect" -ContentType "application/json" -Body $body | ConvertTo-Json -Compress
Start-Sleep -Seconds 3
Invoke-RestMethod "$base/api/status" | ConvertTo-Json -Compress
