# Rebuilds every checked-in kr232 fixture from its source.
#
# The fixtures are images of programs whose sources sit beside them, and
# `kr232_fixtures_match_source` fails when the two drift. Run this after editing
# a fixture source, then commit both files.
#
# KRC and KRLD must be on PATH, or passed as arguments.
param(
    [string]$Krc = 'krc',
    [string]$Krld = 'krld'
)
$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$work = Join-Path $here '../../build/fixtures'
New-Item -ItemType Directory -Force $work | Out-Null

$fixtures = @(
    @{ Source = 'user-exit42.s'; Image = 'user-exit42.k232'; Base = '--base=0x08000000' },
    @{ Source = 'user-calls.s'; Image = 'user-calls.k232'; Base = '--base=0x08000000' },
    @{ Source = 'user-echo.s'; Image = 'user-echo.k232'; Base = '--base=0x08000000' },
    @{ Source = 'user-text.s'; Image = 'user-text.k232'; Base = '--base=0x08000000' },
    @{ Source = 'blank-flip.s'; Image = 'blank-flip.k232'; Base = '' }
)

foreach ($fixture in $fixtures) {
    $source = Join-Path $here $fixture.Source
    $object = Join-Path $work ($fixture.Source -replace '\.s$', '.o')
    $image = Join-Path $here $fixture.Image
    & $Krc -c --target kr232 $source -o $object
    if ($LASTEXITCODE -ne 0) { throw "compiling $($fixture.Source) failed" }
    if ($fixture.Base) {
        & $Krld $fixture.Base --entry=_start $object -o $image
    } else {
        & $Krld --entry=_start $object -o $image
    }
    if ($LASTEXITCODE -ne 0) { throw "linking $($fixture.Image) failed" }
    Write-Output "rebuilt $($fixture.Image)"
}
