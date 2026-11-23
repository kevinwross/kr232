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

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

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
Invoke-Step 'ctest (default)' { ctest --test-dir build/windows-msvc --output-on-failure }

if ($Krc -and $Krld) {
    if (-not (Test-Path $Krc) -or -not (Test-Path $Krld)) {
        throw "KRC and KRLD were given but do not exist: $Krc $Krld"
    }
    Write-Output '== kernel rows, package row, fixture drift'
    $configure = @("-DKR232_KRC=$Krc", "-DKR232_KRLD=$Krld")
    Invoke-Step 'configure with the kernel' {
        cmake -S $root -B build/gate-kernel -G Ninja -DKR232_BUILD_KERNEL=ON @configure
    }
    Invoke-Step 'build' { cmake --build build/gate-kernel -j 8 }
    Invoke-Step 'kernel ctest' { ctest --test-dir build/gate-kernel --output-on-failure }
} else {
    Write-Output '== kernel rows skipped: pass -Krc and -Krld to run them'
}

if ($Arm64) {
    Write-Output '== ARM64'
    Invoke-Step 'check-arm64' { & (Join-Path $PSScriptRoot 'check-arm64.ps1') }
}

Write-Output 'GATE KR232: PASS'
