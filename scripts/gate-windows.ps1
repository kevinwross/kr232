# The kr232 gate: the spec lint, the default suite, and the kernel rows.
#
# The repository had no single entry point, so the checks lived in this order in
# kr232-impl.md and were run by hand. Pass -Krc/-Krld to enable the kernel rows,
# the package row and the fixture drift row; without them those are skipped and
# the gate says so rather than pretending they ran.
#
# Usage: pwsh -File scripts/gate-windows.ps1 [-Krc <krc.exe>] [-Krld <krld.exe>] [-Arm64]
param(
    [string]$Krc = '',
    [string]$Krld = '',
    [switch]$Arm64
)
$ErrorActionPreference = 'Stop'
# J5: ask the doctor first, so a missing tool fails with a sentence naming the
# tool and the repository instead of a build error. The doctor lives in the
# compiler tree, the same place the shell one does.
$doctor = Join-Path (Split-Path -Parent $PSScriptRoot) '../krcompiler/scripts/doctor.ps1'
if (Test-Path $doctor) {
    & pwsh -NoProfile -File $doctor -Repo kr232
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Warning "gate: no doctor at '$doctor'; continuing without one"
}

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# CMake and ctest are not on PATH on this machine; the Visual Studio copy is. Fall
# back to the bare names for a machine where they are on PATH.
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
if (-not (Test-Path $cmake)) { $cmake = 'cmake'; $ctest = 'ctest' }

function Invoke-Step([string]$Label, [scriptblock]$Action) {
    Write-Output "== $Label"
    & $Action
    if ($LASTEXITCODE -ne 0) { throw "$Label failed (exit code $LASTEXITCODE)" }
}

Invoke-Step 'spec lint' { node tools/check-isa.mjs }

Write-Output '== unit tests and the default suite'
$unit = Join-Path $root 'build/windows-msvc/kr232_tests.exe'
if (Test-Path $unit) {
    Invoke-Step 'kr232_tests' { & $unit }
}
Invoke-Step 'ctest (default)' { & $ctest --test-dir build/windows-msvc --output-on-failure }

if ($Krc -and $Krld) {
    if (-not (Test-Path $Krc) -or -not (Test-Path $Krld)) {
        throw "KRC and KRLD were given but do not exist: $Krc $Krld"
    }
    Write-Output '== kernel rows, package row, fixture drift'
    $configure = @("-DKR232_KRC=$Krc", "-DKR232_KRLD=$Krld")
    # A fresh Ninja tree needs cl.exe, its INCLUDE paths and the Windows SDK on the
    # environment: from a plain shell the dyndep step fails with "code=2" and the
    # message names nothing useful. Say what is missing instead.
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -and -not $env:INCLUDE) {
        throw 'the kernel rows configure a fresh Ninja tree, which needs a Developer environment: run this from a Developer PowerShell or after vcvarsall.bat'
    }
    Invoke-Step 'configure with the kernel' {
        & $cmake -S $root -B build/gate-kernel -G Ninja -DKR232_BUILD_KERNEL=ON @configure
    }
    Invoke-Step 'build' { & $cmake --build build/gate-kernel -j 8 }
    # F2: the row count is asserted here and on ARM64, because a row that stops
    # registering reports success and a smaller suite is the failure this exists
    # to catch.
    $registered = (& $ctest --test-dir build/gate-kernel -N 2>&1 | Select-String -Pattern 'Total Tests: (\d+)').Matches.Groups[1].Value
    if ($registered -ne '14') {
        throw "the kernel tree registered $registered rows, expected 14; a missing row reports success"
    }
    Write-Output "kernel rows registered: $registered"
    Invoke-Step 'kernel ctest' { & $ctest --test-dir build/gate-kernel --output-on-failure }
} else {
    Write-Output '== kernel rows skipped: pass -Krc and -Krld to run them'
}

if ($Arm64) {
    Write-Output '== ARM64'
    Invoke-Step 'check-arm64' { & (Join-Path $PSScriptRoot 'check-arm64.ps1') }
}

Write-Output 'GATE KR232: PASS'
