# Rebuilds every checked-in kr232 fixture from its source.
#
# The fixtures are images of programs whose sources sit beside them, and
# `kr232_fixtures_match_source` fails when the two drift. Run this after editing
# a fixture source, then commit both files.
#
# KRC and KRLD must be on PATH, or passed as arguments.
param(
    [string]$Krc = 'krc',
    [string]$Krld = 'krld',
    [string]$Runtime = ''
)
$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
if (-not $Runtime) {
    # The user-mode start-up and the variadic stub sit in the compiler's runtime
    # directory, beside this checkout.
    $Runtime = Join-Path $here '../../../krcompiler/targets/kr232/runtime'
}
if (-not (Test-Path $Runtime)) {
    throw "runtime directory not found: $Runtime (pass -Runtime)"
}

$work = Join-Path $here '../../build/fixtures'
New-Item -ItemType Directory -Force $work | Out-Null

# `Extra` names further sources compiled and linked beside the main one.
$fixtures = @(
    @{ Source = 'user-exit42.s'; Image = 'user-exit42.k232'; Base = '--base=0x08000000' },
    @{ Source = 'user-calls.c'; Image = 'user-calls.k232'; Base = '--base=0x08000000'
       Extra = @('crt0_user.s', 'syscall6.s') },
    @{ Source = 'user-echo.s'; Image = 'user-echo.k232'; Base = '--base=0x08000000' },
    @{ Source = 'user-text.s'; Image = 'user-text.k232'; Base = '--base=0x08000000' },
    @{ Source = 'blank-flip.s'; Image = 'blank-flip.k232'; Base = '' }
)

foreach ($fixture in $fixtures) {
    $source = Join-Path $here $fixture.Source
    $objects = @()
    $main = Join-Path $work (($fixture.Source -replace '\.[sc]$', '') + '.o')
    & $Krc -c --target kr232 $source -o $main
    if ($LASTEXITCODE -ne 0) { throw "compiling $($fixture.Source) failed" }
    $objects += $main
    if ($fixture.Extra) {
        foreach ($extra in $fixture.Extra) {
            $object = Join-Path $work (($extra -replace '\.[sc]$', '') + '.o')
            & $Krc -c --target kr232 (Join-Path $Runtime $extra) -o $object
            if ($LASTEXITCODE -ne 0) { throw "compiling $extra failed" }
            $objects += $object
        }
    }
    $image = Join-Path $here $fixture.Image
    $arguments = @()
    if ($fixture.Base) { $arguments += $fixture.Base }
    $arguments += "--source-root=$here"
    $arguments += '--entry=_start'
    $arguments += $objects
    $arguments += '-o'
    $arguments += $image
    & $Krld @arguments
    if ($LASTEXITCODE -ne 0) { throw "linking $($fixture.Image) failed" }
    Write-Output "rebuilt $($fixture.Image)"
}
