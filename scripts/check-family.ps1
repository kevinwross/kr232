<#
.SYNOPSIS
    Build and gate the whole family in dependency order.

.DESCRIPTION
    One command, one summary, one exit code. The order is the dependency graph:
    the compiler first, then the machines that consume it, then the debugger that
    links them.

    The compiler this run builds is installed into one prefix, and every later
    repository is pointed at that prefix. Without that step the family gate would
    test whatever the last hand install left behind, which is the defect the
    prefix stamp was added to make visible.

    A repository is gated by its own script when it has one -- the point of the
    family gate is to run what each tree declares, not to invent a second opinion
    -- and by configure, build and test on the documented preset when it does not.

    Every repository also declares how many rows its suite registers. A count that
    does not match fails the run, because a row that stops registering still
    reports success and a smaller suite is exactly the failure this catches.

.PARAMETER Arm64
    Run the gate on the ARM64 host instead of this one, through
    scripts/check-arm64.ps1.

.PARAMETER SkipTests
    Configure and build only; the summary says the tests were skipped.

.PARAMETER Only
    Restrict the run to these repositories, e.g. -Only kr32,krdbg.

.PARAMETER Prefix
    Where to install the compiler this run builds. Defaults to
    <family>/build/family-prefix.

.EXAMPLE
    ./scripts/check-family.ps1
    ./scripts/check-family.ps1 -Arm64
#>
[CmdletBinding()]
param(
    [switch]$Arm64,
    [switch]$SkipTests,
    [string[]]$Only = @(),
    [string]$Prefix = '',
    [switch]$All
)

$ErrorActionPreference = 'Stop'

# cmd passes -Only a,b as one string, so each element is split as well.
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

# The script lives in <repo>/scripts, so the family root is two levels up.
$family = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Prefix) { $Prefix = Join-Path $family 'build/family-prefix' }
# A Debug library records its CRT; a Release consumer linking it fails on
# __imp__CrtDbgReport. The two configurations get two prefixes, and each row is
# pointed at the one built for it.
$PrefixRelease = "$Prefix-release"

if ($Arm64) {
    $remote = Join-Path $PSScriptRoot 'check-arm64.ps1'
    if (-not (Test-Path $remote)) { throw "the ARM64 gate is missing: $remote" }
    Write-Host 'family: delegating to the ARM64 host'
    & pwsh -NoProfile -File $remote
    exit $LASTEXITCODE
}

# Dependency order, not alphabetical. Expected is the row count each suite
# registers; kr232 asserts its own two trees inside its gate, so its family
# expectation is 0.
$order = @(
    [pscustomobject]@{ Name = 'krcompiler'; Gate = 'scripts/gate-windows.ps1'; Preset = 'windows-msvc-debug'; Expected = 962 },
    [pscustomobject]@{ Name = 'kr32';       Gate = 'scripts/run-gate.ps1';     Preset = 'windows-msvc-debug'; Expected = 40 },
    [pscustomobject]@{ Name = 'kr16';       Gate = '';                        Preset = 'windows-ninja-debug'; Expected = 61 },
    [pscustomobject]@{ Name = 'kr216';      Gate = '';                        Preset = 'windows-msvc-debug'; Expected = 56 },
    [pscustomobject]@{ Name = 'kr232';      Gate = 'scripts/gate-windows.ps1'; Preset = 'windows-msvc-debug'; Expected = 0 },
    [pscustomobject]@{ Name = 'krdbg';      Gate = '';                        Preset = 'windows-msvc'; Expected = 14 }
)

$results = @()
$failed = 0

foreach ($entry in $order) {
    if ($Only.Count -gt 0 -and $Only -notcontains $entry.Name) { continue }
    $tree = Join-Path $family $entry.Name
    if (-not (Test-Path $tree)) {
        $results += [pscustomobject]@{ Repo = $entry.Name; Mode = 'missing tree'; Result = 'FAIL'; Seconds = 0 }
        $failed++
        continue
    }

    $started = Get-Date
    $mode = ''
    $ok = $false
    try {
        $script = if ($entry.Gate) { Join-Path $tree $entry.Gate } else { '' }
        if ($script -and (Test-Path $script)) {
            $mode = "gate $($entry.Gate)"
            $arguments = @('-NoProfile', '-File', $script)
            if ($SkipTests) { $arguments += '-SkipTests' }
            if ($entry.Name -in @('kr232', 'kr32') -and (Test-Path (Join-Path $Prefix 'bin/krc.exe'))) {
                $arguments += @('-Krc', (Join-Path $Prefix 'bin/krc.exe'),
                                '-Krld', (Join-Path $Prefix 'bin/krld.exe'))
            }
            & pwsh @arguments
            $ok = $LASTEXITCODE -eq 0
        } else {
            $mode = "preset $($entry.Preset)"
            $log = Join-Path $tree "build/family-$($entry.Name).log"
            New-Item -ItemType Directory -Force (Split-Path -Parent $log) | Out-Null
            Push-Location $tree
            try {
                cmake --preset $entry.Preset "-DCMAKE_PREFIX_PATH=$Prefix" *>> $log
                if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
                cmake --build --preset $entry.Preset *>> $log
                if ($LASTEXITCODE -ne 0) { throw 'build failed' }
                if (-not $SkipTests) {
                    ctest --preset $entry.Preset *>> $log
                    if ($LASTEXITCODE -ne 0) { throw 'tests failed' }
                }
                $ok = $true
            } catch {
                Write-Host "family: $($entry.Name) $($_.Exception.Message)" -ForegroundColor Red
                Get-Content $log -Tail 12 | ForEach-Object { Write-Host "  | $_" }
            } finally {
                Pop-Location
            }
        }

        if ($entry.Name -eq 'krcompiler' -and $ok) {
            Push-Location $tree
            try {
                cmake --install (Join-Path $tree "build/$($entry.Preset)") --config Debug --prefix $Prefix *> $null
                if ($LASTEXITCODE -ne 0) { throw 'install failed' }
                Write-Host "family: compiler installed into $Prefix"

            } finally { Pop-Location }
        }

        if ($ok -and -not $SkipTests -and $entry.Expected -gt 0) {
            Push-Location $tree
            try {
                $countLine = (ctest --preset $entry.Preset -N 2>&1 | Select-String -Pattern 'Total Tests: (\d+)' | Select-Object -First 1)
                $rows = if ($countLine) { $countLine.Matches.Groups[1].Value } else { '0' }
                if ("$rows" -ne "$($entry.Expected)") {
                    Write-Host "family: $($entry.Name) registered $rows rows, expected $($entry.Expected); a missing row reports success" -ForegroundColor Red
                    $ok = $false
                } else {
                    Write-Host "family: $($entry.Name) rows registered: $rows"
                }
            } finally { Pop-Location }
        }
    } catch {
        Write-Host "family: $($entry.Name) $($_.Exception.Message)" -ForegroundColor Red
        $ok = $false
    }

    if (-not $ok) { $failed++ }
    $results += [pscustomobject]@{
        Repo    = $entry.Name
        Mode    = $mode
        Result  = if ($ok) { 'pass' } else { 'FAIL' }
        Seconds = [int]((Get-Date) - $started).TotalSeconds
    }
}


# J1: the table decides the default, and the full matrix is always one command.
if ($All) {
    Write-Host ''
    Write-Host 'family: release rows'
    foreach ($entry in $order) {
        if ($Only.Count -gt 0 -and $Only -notcontains $entry.Name) { continue }
        $tree = Join-Path $family $entry.Name
        # krdbg names its presets windows-msvc / windows-msvc-release, so the
        # mapping appends when there is no -debug suffix to replace.
        $preset = if ($entry.Preset -match '-debug$') {
            $entry.Preset -replace '-debug$', '-release'
        } else {
            "$($entry.Preset)-release"
        }
        $started = Get-Date
        $log = Join-Path $tree "build/family-release.log"
        Push-Location $tree
        try {
            # --fresh: a build directory that has already found the Debug package keeps krc-toolchain_DIR pinned there whatever CMAKE_PREFIX_PATH says.
            cmake --preset $preset --fresh "-DCMAKE_PREFIX_PATH=$PrefixRelease" *>> $log
            if ($LASTEXITCODE -ne 0) { throw "configure failed" }
            cmake --build --preset $preset *>> $log
            if ($LASTEXITCODE -ne 0) { throw "build failed" }
            if (-not $SkipTests) {
                ctest --preset $preset *>> $log
                if ($LASTEXITCODE -ne 0) { throw "tests failed" }
            }
            $results += [pscustomobject]@{ Repo = "$($entry.Name) (release)"; Mode = "preset $preset"; Result = 'pass'; Seconds = [int]((Get-Date) - $started).TotalSeconds }
            # The release rows that come next link the compiler, so it is installed
            # from the tree this row just built and tested -- not from the Debug one,
            # whose Release configuration may never have been built.
            if ($entry.Name -eq 'krcompiler') {
                cmake --install (Join-Path $tree "build/$preset") --prefix $PrefixRelease *> $null
                if ($LASTEXITCODE -ne 0) { throw 'release install failed' }
                Write-Host "family: release compiler installed into $PrefixRelease"
            }
        } catch {
            Write-Host "family: $($entry.Name) release $($_.Exception.Message)" -ForegroundColor Red
            Get-Content $log -Tail 8 | ForEach-Object { Write-Host "  | $_" }
            $results += [pscustomobject]@{ Repo = "$($entry.Name) (release)"; Mode = "preset $preset"; Result = 'FAIL'; Seconds = [int]((Get-Date) - $started).TotalSeconds }
            $failed++
        } finally { Pop-Location }
    }

    Write-Host ''
    Write-Host 'family: Linux x64 rows in WSL'
    $wslScript = 'set -e; for r in krcompiler kr32 kr16 kr216 kr232 krdbg; do d=$HOME/mirrors/$r; sh /mnt/c/Users/kevin/repos/krcompiler/scripts/kr-mirror.sh $r >/dev/null 2>&1 || true; ( cd $d && if [ -x scripts/gate-linux.sh ]; then bash scripts/gate-linux.sh; elif [ -x scripts/run-gate.sh ]; then bash scripts/run-gate.sh; else cmake --preset linux-gcc-debug >/dev/null && cmake --build --preset linux-gcc-debug >/dev/null && ctest --preset linux-gcc-debug; fi ) > /tmp/family-$r.log 2>&1 && echo "$r pass" || echo "$r FAIL"; done'
    $started = Get-Date
    $wslOut = & wsl.exe -e bash -lc $wslScript 2>&1
    foreach ($line in $wslOut) { Write-Host "family: $line" }
    $wslResult = if ($wslOut -match 'FAIL') { 'FAIL' } else { 'pass' }
    $results += [pscustomobject]@{ Repo = 'linux x64 (WSL)'; Mode = 'gate-linux from ~/mirrors'; Result = $wslResult; Seconds = [int]((Get-Date) - $started).TotalSeconds }
    if ($wslResult -eq 'FAIL') { $failed++ }
}

Write-Host ''
Write-Host 'family summary'
foreach ($row in $results) {
    Write-Host ("  {0,-11} {1,-34} {2,-5} {3,4}s" -f $row.Repo, $row.Mode, $row.Result, $row.Seconds)
}
if ($SkipTests) {
    Write-Host '  tests were skipped by -SkipTests; this run proves the trees build'
}
if ($failed -ne 0) {
    Write-Host "family: $failed of $($results.Count) failed" -ForegroundColor Red
    exit 1
}
Write-Host "family: $($results.Count) of $($results.Count) passed"
