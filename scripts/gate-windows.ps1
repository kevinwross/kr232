<#
.SYNOPSIS
    Runs the kr232 gate on Windows: configure, build, test, spec lint.

.DESCRIPTION
    CMake and ctest exist only inside the Visual Studio developer environment on
    this machine (kr232-impl.md §1), so the caller initialises it first:

        cmd /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"" x64 && pwsh -File scripts\gate-windows.ps1"

    The script refuses to guess a compiler: if cmake is not on PATH it says so
    and exits 2 instead of configuring with whatever it finds.
#>
#requires -Version 7

param(
    [string]$Preset = "windows-msvc"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "cmake is not on PATH. Initialise the Visual Studio developer environment first (see the header of this script)."
    exit 2
}

Write-Host "kr232 gate: $Preset"
cmake --preset $Preset
cmake --build --preset $Preset
ctest --preset $Preset
node tools/check-isa.mjs
Write-Host "kr232 gate: $Preset passed"
