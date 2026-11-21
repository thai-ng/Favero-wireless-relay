# Fire a hit on the OptiPlex emulator: .\hit.ps1 -Side left
# (side = left or right). Builds the JSON with ConvertTo-Json so
# cmd.exe quoting cannot mangle it (see connect_central.ps1).
param([string]$Side = "left")
$ErrorActionPreference = "Stop"
$base = "http://192.168.50.133:8765"
$body = @{ side = $Side } | ConvertTo-Json
Invoke-RestMethod -Method Post -Uri "$base/api/hit" -ContentType "application/json" -Body $body | ConvertTo-Json -Compress
