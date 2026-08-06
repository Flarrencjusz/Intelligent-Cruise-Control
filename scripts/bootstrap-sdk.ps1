# SPDX-License-Identifier: GPL-3.0-only

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolsDirectory = Join-Path $projectRoot ".tools"
$archivePath = Join-Path $toolsDirectory "scs_sdk_1_14.zip"
$sdkDirectory = Join-Path $toolsDirectory "scs_sdk"
$expectedSha256 = "C6C1F7376B7324994D9F9C567F3C4141FBBF305B6BF803BC4CFEEF2437B2023A"

if (Test-Path -LiteralPath (Join-Path $sdkDirectory "include\scssdk.h")) {
    Write-Host "Official SCS SDK headers are already installed."
    exit 0
}

New-Item -ItemType Directory -Force -Path $toolsDirectory | Out-Null
Invoke-WebRequest -Uri "https://download.eurotrucksimulator2.com/scs_sdk_1_14.zip" -OutFile $archivePath

$actualSha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
if ($actualSha256 -ne $expectedSha256) {
    throw "SCS SDK archive checksum mismatch. Expected $expectedSha256, received $actualSha256."
}

Expand-Archive -LiteralPath $archivePath -DestinationPath $sdkDirectory -Force
Write-Host "Installed official SCS Telemetry & Input SDK 1.14 under .tools\scs_sdk."
