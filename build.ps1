# SPDX-License-Identifier: GPL-3.0-only
param(
    [switch]$ReleasePackage,
    [string]$Version = "1.1.0"
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
$sdkInclude = Join-Path $projectRoot ".tools\scs_sdk\include"
$buildDirectory = Join-Path $projectRoot "build"
$outputDirectory = Join-Path $buildDirectory "release"
$vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path -LiteralPath $sdkInclude)) {
    throw "Official SCS SDK headers are missing: $sdkInclude"
}
if (-not (Test-Path -LiteralPath $vsWhere)) {
    throw "Visual Studio Installer's vswhere.exe was not found. Install Visual Studio 2022 with Desktop development with C++."
}

$vsInstall = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) {
    throw "No Visual Studio installation with the x64 C++ toolchain was found."
}
$vsDevCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"

$resolvedProject = [IO.Path]::GetFullPath($projectRoot).TrimEnd('\') + '\'
$resolvedOutput = [IO.Path]::GetFullPath($outputDirectory)
if (-not $resolvedOutput.StartsWith($resolvedProject, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean output outside the project: $resolvedOutput"
}
if (Test-Path -LiteralPath $outputDirectory) {
    Remove-Item -LiteralPath $outputDirectory -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $buildDirectory, $outputDirectory | Out-Null

$testCompile = @(
    "cl /nologo /std:c++17 /EHsc /W4 /WX"
    "`"$projectRoot\tests\adjustment_controller_tests.cpp`""
    "/Fo:`"$buildDirectory\adjustment_controller_tests.obj`""
    "/Fe:`"$buildDirectory\adjustment_controller_tests.exe`""
) -join " "
$testCommand =
    "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 && $testCompile"
cmd.exe /d /s /c $testCommand
if ($LASTEXITCODE -ne 0) { throw "Test build failed." }

& (Join-Path $buildDirectory "adjustment_controller_tests.exe")
if ($LASTEXITCODE -ne 0) { throw "Unit tests failed." }

$pluginCompile = @(
    "cl /nologo /std:c++17 /EHsc /O2 /W4 /WX /LD"
    "`"$projectRoot\src\plugin.cpp`""
    "/I`"$sdkInclude`""
    "/Fo:`"$buildDirectory\plugin.obj`""
    "/link /DEF:`"$projectRoot\src\exports.def`""
    "/OUT:`"$outputDirectory\intelligent_cruise_control.dll`""
    "/IMPLIB:`"$buildDirectory\intelligent_cruise_control.lib`""
    "/PDB:`"$buildDirectory\intelligent_cruise_control.pdb`""
    "user32.lib gdi32.lib"
) -join " "
$pluginCommand =
    "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 && $pluginCompile"
cmd.exe /d /s /c $pluginCommand
if ($LASTEXITCODE -ne 0) { throw "Plugin build failed." }

Copy-Item -LiteralPath (Join-Path $projectRoot "README.md") -Destination $outputDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot "LICENSE") -Destination $outputDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot "CHANGELOG.md") -Destination $outputDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot "config\settings.ini.example") -Destination $outputDirectory -Force
New-Item -ItemType Directory -Force -Path (Join-Path $outputDirectory "scripts") | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot "scripts\install.ps1") -Destination (Join-Path $outputDirectory "scripts") -Force
Copy-Item -LiteralPath (Join-Path $projectRoot "scripts\uninstall.ps1") -Destination (Join-Path $outputDirectory "scripts") -Force

if ($ReleasePackage) {
    $packagePath = Join-Path $buildDirectory "Intelligent_Cruise_Control_${Version}_ETS2_1.60.zip"
    if (Test-Path -LiteralPath $packagePath) {
        Remove-Item -LiteralPath $packagePath -Force
    }
    Compress-Archive -Path (Join-Path $outputDirectory "*") -DestinationPath $packagePath
    Write-Host "Created release package: $packagePath"
}

Write-Host "Build completed: $outputDirectory"
