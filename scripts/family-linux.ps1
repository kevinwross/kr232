<#
.SYNOPSIS
    The Linux x64 half of the family gate: every repository's gate in WSL, from the
    ext4 mirrors.

.DESCRIPTION
    Run by check-family.ps1 in the background while the Windows waves work, because
    the two halves share nothing but the machine. It writes one line per repository
    to its results file -- `<repo> pass|FAIL`, then `seconds <n>` -- so the caller
    can join without a PowerShell job, whose output channel breaks when a native
    process writes to stderr.

    The compiler is built and installed first: the consumers link its package, and
    an installed library older than the headers it is compiled against corrupts
    memory instead of failing to link.

.PARAMETER Logs
    Directory for the generated shell scripts and the results file.

.PARAMETER Jobs
    Job count each gate is given, so six shells do not each take the whole machine.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Logs,
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$repositories = @('krcompiler', 'kr32', 'kr16', 'kr216', 'kr232', 'krdbg')
# The Linux wave runs in WSL on x86_64: GCC 15 there intermittently segfaults
# while garbage-collecting, so those rows stay at eight jobs even when the family
# gate offers more. An ARM64 host would take the number it is given.
$LinuxJobs = if ($Jobs -gt 8) { 8 } else { $Jobs }
$results = Join-Path $Logs 'linux-results.txt'
$started = Get-Date
$lines = @()
New-Item -ItemType Directory -Force $Logs | Out-Null

function Write-Shell([string]$path, [string]$body) {
    # LF only: bash reads `fi\r` as a word, not a keyword.
    [System.IO.File]::WriteAllText($path, ($body -replace "`r`n", "`n"))
}

function Start-Shell([string]$path) {
    $wslPath = ($path -replace '\\', '/') -replace '^C:', '/mnt/c'
    return Start-Process -FilePath 'wsl.exe' -ArgumentList @('-e', 'bash', $wslPath) -NoNewWindow -PassThru
}

$setup = @"
set -u
export PATH="`$HOME/.local/bin:`$PATH"
sh /mnt/c/Users/kevin/repos/krcompiler/scripts/kr-mirror.sh krcompiler >/dev/null 2>&1 || true
cd "`$HOME/mirrors/krcompiler" || exit 2
bash scripts/gate-linux.sh
gate_status=`$?
cmake --install build/linux-gcc-debug --prefix "`$HOME/krc-debug" >/dev/null
cmake --install build/linux-gcc-debug --prefix "`$HOME/.local" >/dev/null
# The installs run even when the gate failed, so the gate status is what the
# caller must see: a script ending on the last install reports success.
exit `$gate_status
"@
$setupPath = Join-Path $Logs 'linux-krcompiler-setup.sh'
Write-Shell $setupPath $setup
$setupProcess = Start-Shell $setupPath
$setupProcess.WaitForExit()
$lines += ('krcompiler ' + $(if ($setupProcess.ExitCode -eq 0) { 'pass' } else { 'FAIL' }))

$processes = @()
foreach ($repository in ($repositories | Where-Object { $_ -ne 'krcompiler' })) {
    $one = @"
set -u
export PATH="`$HOME/.local/bin:`$PATH"
export FAMILY_JOBS=$LinuxJobs
r=$repository
d=`$HOME/mirrors/`$r
sh /mnt/c/Users/kevin/repos/krcompiler/scripts/kr-mirror.sh `$r >/dev/null 2>&1 || true
cd "`$d" || exit 2
if [ -x scripts/gate-linux.sh ]; then bash scripts/gate-linux.sh
elif [ -x scripts/run-gate.sh ]; then bash scripts/run-gate.sh
else cmake --preset linux-gcc-debug >/dev/null && cmake --build --preset linux-gcc-debug -j $LinuxJobs >/dev/null && ctest --preset linux-gcc-debug -j $LinuxJobs; fi
"@
    $onePath = Join-Path $Logs "linux-$repository.sh"
    Write-Shell $onePath $one
    $processes += [pscustomobject]@{ Repo = $repository; Process = (Start-Shell $onePath) }
}

foreach ($row in $processes) {
    $row.Process.WaitForExit()
    $lines += ('{0} {1}' -f $row.Repo, $(if ($row.Process.ExitCode -eq 0) { 'pass' } else { 'FAIL' }))
}
$lines += ('seconds ' + [int]((Get-Date) - $started).TotalSeconds)
Set-Content -LiteralPath $results -Value $lines
