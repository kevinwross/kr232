<#
.SYNOPSIS
    Build and gate the whole family: one command, one summary, one exit code.

.DESCRIPTION
    The order is the dependency graph. The compiler is built and installed first, in
    each configuration, because every other repository links its package; after that
    the repositories are independent and run at the same time.

    Waves:
      1. the compiler, Debug; install into the Debug prefix
      2. every consumer, Debug, in parallel
      3. the compiler, Release; install into the Release prefix -- two prefixes,
         because a Release consumer linking a Debug library fails on
         __imp__CrtDbgReport
      4. every consumer, Release, in parallel
      5. the Linux x64 rows in WSL, started before the Windows waves so the slowest
         wave runs underneath the others

    Each repository is gated by its own script when it has one, and by configure,
    build and test on its documented preset when it does not. Row counts are
    asserted: a suite that registers fewer rows still reports success.

.PARAMETER Arm64
    Run the gate on the ARM64 host instead of this one.

.PARAMETER SkipTests
    Configure and build only.

.PARAMETER Only
    Restrict to these repositories, e.g. -Only kr32,krdbg.

.PARAMETER Serial
    One repository at a time. For reading the log of a failing row.

.PARAMETER Jobs
    Repositories per wave. Defaults to the smaller of six and half the cores.

.EXAMPLE
    ./scripts/check-family.ps1
    ./scripts/check-family.ps1 -Arm64
#>
[CmdletBinding()]
param(
    [switch]$Arm64,
    [switch]$SkipTests,
    [string[]]$Only = @(),
    [switch]$All,
    [switch]$Serial,
    [int]$Jobs = 0,
    [string]$Prefix = ''
)

$ErrorActionPreference = 'Stop'
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

$family = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Prefix) { $Prefix = Join-Path $family 'build/family-prefix' }
$PrefixRelease = "$Prefix-release"

if ($Arm64) {
    $remote = Join-Path $PSScriptRoot 'check-arm64.ps1'
    if (-not (Test-Path $remote)) { throw "the ARM64 gate is missing: $remote" }
    Write-Host 'family: delegating to the ARM64 host'
    & pwsh -NoProfile -File $remote
    exit $LASTEXITCODE
}

$cores = [Environment]::ProcessorCount
if ($Jobs -le 0) { $Jobs = [Math]::Max(1, [Math]::Min(6, [Math]::Floor($cores / 2))) }
$waveJobs = if ($Serial) { 1 } else { [Math]::Max(1, [Math]::Min($Jobs, 6)) }
$innerJobs = [Math]::Max(1, [Math]::Floor($cores / $waveJobs))

$worker = Join-Path $PSScriptRoot 'family-worker.ps1'
$logs = Join-Path $family 'build/family-logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$results = @()
$failed = 0

$compiler = [pscustomobject]@{ Name = 'krcompiler'; Gate = 'scripts/gate-windows.ps1'; Preset = 'windows-msvc-debug'; Expected = 976; ExpectedRelease = 976 }
$consumers = @(
    [pscustomobject]@{ Name = 'kr32';   Gate = 'scripts/run-gate.ps1';     Preset = 'windows-msvc-debug'; Expected = 41; ExpectedRelease = 41 },
    [pscustomobject]@{ Name = 'kr16';   Gate = '';                        Preset = 'windows-ninja-debug'; Expected = 61; ExpectedRelease = 61 },
    [pscustomobject]@{ Name = 'kr216';  Gate = '';                        Preset = 'windows-msvc-debug'; Expected = 56; ExpectedRelease = 11 },
    [pscustomobject]@{ Name = 'kr232';  Gate = 'scripts/gate-windows.ps1'; Preset = 'windows-msvc-debug'; Expected = 0; ExpectedRelease = 0 },
    [pscustomobject]@{ Name = 'krdbg';  Gate = '';                        Preset = 'windows-msvc'; Expected = 14; ExpectedRelease = 3 }
)

# The Linux half needs nothing from the Windows waves, so it starts first and runs
# underneath them. It is a separate script with a results file, not a PowerShell
# job: a job serialises its output back and a native process writing to stderr
# breaks that channel.
$linuxProcess = $null
$linuxResults = Join-Path $logs 'linux-results.txt'
if (-not $Only -and -not $SkipTests) {
    $runner = Join-Path $PSScriptRoot 'family-linux.ps1'
    $linuxProcess = Start-Process -FilePath 'pwsh' `
        -ArgumentList @('-NoProfile', '-File', $runner, '-Logs', $logs, '-Jobs', $innerJobs) `
        -NoNewWindow -PassThru
    Write-Host 'family: Linux x64 rows started in the background'
}

function Invoke-Wave {
    param([object[]]$Entries, [string]$Label, [string]$PrefixForWave)

    $running = @()
    $index = 0
    while ($index -lt $Entries.Count -or $running.Count -gt 0) {
        while ($running.Count -lt $waveJobs -and $index -lt $Entries.Count) {
            $entry = $Entries[$index]
            $index++
            $log = Join-Path $logs "$Label-$($entry.Name).log"
            $arguments = @('-NoProfile', '-File', $worker,
                           '-Repo', $entry.Name, '-Preset', $entry.Preset,
                           '-Expected', $entry.Expected,
                           '-Prefix', $PrefixForWave, '-Jobs', $innerJobs, '-Log', $log)
            if ($entry.Gate) { $arguments += @('-Gate', $entry.Gate) }
            if ($SkipTests) { $arguments += '-SkipTests' }
            $running += [pscustomobject]@{
                Entry   = $entry
                Process = Start-Process -FilePath 'pwsh' -ArgumentList $arguments -NoNewWindow -PassThru
                Log     = $log
                Started = Get-Date
            }
        }
        if ($running.Count -eq 0) { break }
        $finished = @($running | Where-Object { $_.Process.HasExited })
        if ($finished.Count -eq 0) { Start-Sleep -Milliseconds 500; continue }
        foreach ($row in $finished) {
            $running = @($running | Where-Object { $_.Process.Id -ne $row.Process.Id })
            $ok = $row.Process.ExitCode -eq 0
            if (-not $ok) {
                $script:failed++
                Write-Host "family: $($row.Entry.Name) failed" -ForegroundColor Red
                Get-Content $row.Log -Tail 10 | ForEach-Object { Write-Host "  | $_" }
            }
            $countLine = Select-String -Path $row.Log -Pattern 'rows registered: (\d+)' -ErrorAction SilentlyContinue |
                         Select-Object -Last 1
            if ($countLine) { Write-Host "family: $($row.Entry.Name) rows registered: $($countLine.Matches.Groups[1].Value)" }
            $script:results += [pscustomobject]@{
                Repo    = $row.Entry.Name
                Mode    = if ($row.Entry.Gate) { 'gate' } else { 'preset' }
                Result  = if ($ok) { 'pass' } else { 'FAIL' }
                Seconds = [int]((Get-Date) - $row.Started).TotalSeconds
            }
        }
    }
}

function Install-Compiler([string]$buildTree, [string]$config, [string]$destination) {
    Push-Location (Join-Path $family 'krcompiler')
    try {
        if ($config) {
            cmake --install $buildTree --config $config --prefix $destination *> $null
        } else {
            cmake --install $buildTree --prefix $destination *> $null
        }
        if ($LASTEXITCODE -ne 0) { throw "installing the compiler into $destination failed" }
        Write-Host "family: compiler installed into $destination"
        return $true
    } catch {
        Write-Host "family: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    } finally {
        Pop-Location
    }
}

# Wave 1 and installation.
$wanted = { param($entry) $Only.Count -eq 0 -or $Only -contains $entry.Name }
if (& $wanted $compiler) {
    $before = $failed
    Invoke-Wave -Entries @($compiler) -Label 'debug' -PrefixForWave $Prefix
    if ($failed -eq $before) {
        if (-not (Install-Compiler "build/$($compiler.Preset)" 'Debug' $Prefix)) { $failed++ }
    }
}

# Wave 2.
$chosen = @($consumers | Where-Object { & $wanted $_ })
if ($chosen.Count -gt 0) {
    Invoke-Wave -Entries $chosen -Label 'debug' -PrefixForWave $Prefix
}

# Waves 3 and 4: Release.
if (-not $SkipTests) {
    if (& $wanted $compiler) {
        $releasePreset = $compiler.Preset -replace '-debug$', '-release'
        $releaseRow = [pscustomobject]@{ Name = 'krcompiler'; Gate = ''; Preset = $releasePreset; Expected = $compiler.ExpectedRelease }
        $before = $failed
        Invoke-Wave -Entries @($releaseRow) -Label 'release' -PrefixForWave $PrefixRelease
        if ($failed -eq $before) {
            if (-not (Install-Compiler "build/$releasePreset" '' $PrefixRelease)) { $failed++ }
        }
    }
    if ($chosen.Count -gt 0) {
        $releaseRows = @($chosen | ForEach-Object {
            $preset = if ($_.Preset -match '-debug$') { $_.Preset -replace '-debug$', '-release' } else { "$($_.Preset)-release" }
            [pscustomobject]@{ Name = $_.Name; Gate = ''; Preset = $preset; Expected = $_.ExpectedRelease }
        })
        Invoke-Wave -Entries $releaseRows -Label 'release' -PrefixForWave $PrefixRelease
    }
}

# Join the Linux half.
if ($linuxProcess) {
    $linuxProcess.WaitForExit()
    if (Test-Path $linuxResults) {
        foreach ($line in (Get-Content -LiteralPath $linuxResults)) {
            if ($line -match '^seconds (\d+)$') { Write-Host "family: linux x64 rows took $($Matches[1])s"; continue }
            if ($line -match '^(\S+) (pass|FAIL)$') {
                $name = $Matches[1]
                $state = $Matches[2]
                Write-Host ("family: {0} {1}" -f $name, $state)
                if ($state -eq 'FAIL') { $script:failed++ }
                $script:results += [pscustomobject]@{ Repo = "$name (linux x64)"; Mode = 'gate-linux'; Result = $state; Seconds = 0 }
            }
        }
    } else {
        Write-Host 'family: the Linux wave wrote no results' -ForegroundColor Red
        $script:failed++
    }
}

Write-Host ''
Write-Host 'family summary'
foreach ($row in $results) {
    Write-Host ("  {0,-24} {1,-10} {2,-5} {3,4}s" -f $row.Repo, $row.Mode, $row.Result, $row.Seconds)
}
if ($SkipTests) { Write-Host '  tests were skipped by -SkipTests; this run proves the trees build' }
Write-Host ("family: {0} cores, {1} repositories at a time, {2} jobs each" -f $cores, $waveJobs, $innerJobs)
if ($failed -ne 0) {
    Write-Host "family: $failed of $($results.Count) failed" -ForegroundColor Red
    exit 1
}
Write-Host "family: $($results.Count) of $($results.Count) passed"
