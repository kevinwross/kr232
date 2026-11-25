<#
.SYNOPSIS
    One repository's share of the family gate, so the gate can run several at once.

.DESCRIPTION
    The family gate used to do each repository's work inline, one after another, so
    a fast machine spent most of the matrix waiting. The work splits cleanly: the
    compiler must finish before anything links it, and after that every repository
    is independent.

    This worker does one repository's row and returns an exit code: it runs the
    repository's own gate script when it has one, and configures, builds and tests
    the documented preset when it does not. The row count is checked here as well as
    in the gate, because a repository that registers fewer rows still reports
    success.

.PARAMETER Repo
    Directory name under the family root.

.PARAMETER Preset
    The preset to build and test when the repository has no gate script.

.PARAMETER Gate
    The repository's own gate script, relative to its root. Empty for none.

.PARAMETER Expected
    Row count to assert. Zero means the repository asserts its own.

.PARAMETER Prefix
    The compiler prefix consumers link against.

.PARAMETER Jobs
    Build and test parallelism inside this worker, so several workers can share one
    machine without oversubscribing it.

.PARAMETER Log
    Where to write the output. The gate reads the tail when a worker fails.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [string]$Preset = 'windows-msvc-debug',
    [string]$Gate = '',
    [int]$Expected = 0,
    [string]$Prefix = '',
    [int]$Jobs = 4,
    # Extra configure arguments for this repository, comma separated: a tree whose
    # guest rows are opt-in needs its option set, and the row table is where that
    # knowledge belongs.
    [string]$Options = '',
    [Parameter(Mandatory = $true)][string]$Log,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$family = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$tree = Join-Path $family $Repo
Set-Location $tree

function Write-Log([string]$text) {
    Add-Content -LiteralPath $Log -Value $text
}

if (-not (Test-Path $tree)) {
    Write-Log "family/$Repo`: missing tree"
    exit 2
}
"" | Set-Content -LiteralPath $Log

try {
    $script = if ($Gate) { Join-Path $tree $Gate } else { '' }
    if ($script -and (Test-Path $script)) {
        Write-Log "== $Repo gate $Gate"
        # A gate script sizes its own builds; this tells it how much of the machine
        # it may take, so six workers do not each ask for all of it.
        $env:FAMILY_JOBS = "$Jobs"
        $arguments = @('-NoProfile', '-File', $script)
        if ($SkipTests) { $arguments += '-SkipTests' }
        # A gate that can run guest rows takes the toolchain; one that cannot ignores
        # the extra arguments. Without this the kernel and package rows were skipped
        # in every family run, which reported fewer rows than the gate itself does.
        if ($Prefix) {
            $krc = Join-Path $Prefix 'bin/krc.exe'
            $krld = Join-Path $Prefix 'bin/krld.exe'
            if (Test-Path $krc) { $arguments += @('-Krc', $krc) }
            if (Test-Path $krld) { $arguments += @('-Krld', $krld) }
        }
        & pwsh @arguments *>&1 | ForEach-Object { Write-Log "$_" }
        if ($LASTEXITCODE -ne 0) { Write-Log "family/$Repo`: gate failed ($LASTEXITCODE)"; exit 1 }
    } else {
        Write-Log "== $Repo preset $Preset"
        $configure = @('--preset', $Preset)
        $extra = @($Options -split ',' | Where-Object { $_ })
        if ($Prefix) {
            $configure += "-DCMAKE_PREFIX_PATH=$Prefix"
            # Both spellings: the family convention is KRG_KRC/KRG_KRLD, and several
            # trees decide whether their guest rows exist from KRC_EXECUTABLE and
            # KRLD_EXECUTABLE. A row that forwards only one silently registers a
            # smaller suite, which is what the row counts exist to catch.
            $krc = Join-Path $Prefix 'bin/krc.exe'
            $krld = Join-Path $Prefix 'bin/krld.exe'
            if ($extra) { $configure += $extra }
            if (Test-Path $krc) { $configure += "-DKRG_KRC=$krc"; $configure += "-DKRC_EXECUTABLE=$krc" }
            if (Test-Path $krld) { $configure += "-DKRG_KRLD=$krld"; $configure += "-DKRLD_EXECUTABLE=$krld" }
        }
        & cmake @configure *>&1 | ForEach-Object { Write-Log "$_" }
        if ($LASTEXITCODE -ne 0) { Write-Log "family/$Repo`: configure failed"; exit 1 }
        & cmake --build --preset $Preset -j $Jobs *>&1 | ForEach-Object { Write-Log "$_" }
        if ($LASTEXITCODE -ne 0) { Write-Log "family/$Repo`: build failed"; exit 1 }
        if (-not $SkipTests) {
            & ctest --preset $Preset -j $Jobs *>&1 | ForEach-Object { Write-Log "$_" }
            if ($LASTEXITCODE -ne 0) { Write-Log "family/$Repo`: tests failed"; exit 1 }
        }
    }

    if ($Expected -gt 0 -and -not $SkipTests) {
        $count = (& ctest --preset $Preset -N 2>&1 | Select-String -Pattern 'Total Tests: (\d+)' |
                  Select-Object -First 1)
        $rows = if ($count) { $count.Matches.Groups[1].Value } else { '0' }
        if ("$rows" -ne "$Expected") {
            Write-Log "family/$Repo`: registered $rows rows, expected $Expected; a missing row reports success"
            exit 1
        }
        Write-Log "family/$Repo`: rows registered: $rows"
    }
    exit 0
} catch {
    Write-Log "family/$Repo`: $($_.Exception.Message)"
    exit 1
}
