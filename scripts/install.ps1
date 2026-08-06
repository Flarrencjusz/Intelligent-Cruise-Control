# SPDX-License-Identifier: GPL-3.0-only
param(
    [string]$GamePath = "C:\Program Files (x86)\Steam\steamapps\common\Euro Truck Simulator 2"
)

$ErrorActionPreference = "Stop"
$releaseRoot = Split-Path -Parent $PSScriptRoot
$dllPath = Join-Path $releaseRoot "intelligent_cruise_control.dll"
$pluginDirectory = Join-Path $GamePath "bin\win_x64\plugins"

if (-not (Test-Path -LiteralPath $dllPath)) {
    throw "Plugin DLL not found: $dllPath"
}
if (-not (Test-Path -LiteralPath (Join-Path $GamePath "bin\win_x64\eurotrucks2.exe"))) {
    throw "ETS2 executable not found under: $GamePath"
}

New-Item -ItemType Directory -Force -Path $pluginDirectory | Out-Null
Copy-Item -LiteralPath $dllPath -Destination $pluginDirectory -Force

Write-Host "Installed Intelligent Cruise Control to:"
Write-Host "  $pluginDirectory"
Write-Host "Press Page Up in game to show or hide the mini HUD."
