# SPDX-License-Identifier: GPL-3.0-only
param(
    [string]$GamePath = "C:\Program Files (x86)\Steam\steamapps\common\Euro Truck Simulator 2"
)

$ErrorActionPreference = "Stop"
$pluginPath = Join-Path $GamePath "bin\win_x64\plugins\intelligent_cruise_control.dll"

if (Test-Path -LiteralPath $pluginPath) {
    Remove-Item -LiteralPath $pluginPath -Force
    Write-Host "Removed: $pluginPath"
} else {
    Write-Host "Plugin is not installed: $pluginPath"
}

Write-Host "Saved preferences remain in:"
Write-Host "  $env:LOCALAPPDATA\IntelligentCruiseControl\settings.ini"
