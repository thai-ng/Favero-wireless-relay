# App-only flash for the XIAO ESP32-S3 side unit (preserves NVS/pairing).
# S3 offsets: bootloader 0x0, partitions 0x8000, app 0x10000.
# DO NOT use the ESP32-classic 0x1000 bootloader offset — on the S3
# 0x1000 is inside the bootloader image, and writing there corrupts
# it (caused the 2026-11-08 "Invalid image block" boot loop).
# Usage: .\flash_side_app_s3.ps1 -Port COM7   (OptiPlex: -Port COM3)
param([string]$Port = "COM7")
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$esptool = ".\tools\esptool-windows-amd64\esptool.exe"
if (-not (Test-Path $esptool)) { $esptool = "esptool" }  # pip install on PATH (OptiPlex)
& $esptool --chip esp32s3 --port $Port --baud 460800 write-flash 0x0 epee_side_xiao_s3.ino.bootloader.bin 0x8000 epee_side_xiao_s3.ino.partitions.bin 0x10000 epee_side_xiao_s3.ino.bin
