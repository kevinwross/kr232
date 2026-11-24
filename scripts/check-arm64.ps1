# Runs the kr232 checks on the ARM64 host over ssh, including the compiler's whole
#
# The Linux host is the only place the ARM64 toolchain exists, so every check
# there has been run by hand and its result pasted into kr232-impl.md. This does
# suite and krdbg's own gate. The steps are the same in one command and it fails
# loudly: pull the three checkouts, build the compiler and the emulator, then run
#
# every suite the repositories have. Usage: scripts/check-arm64.ps1 [-Target bitwhiz] [-Root ~/repos]
param(
    [string]$Target = 'bitwhiz',
    [string]$Root = '~/repos'
)
$ErrorActionPreference = 'Stop'

$remote = @'
set -e
ROOT="__ROOT__"
BUILD="$ROOT/kr232/build/arm64"
git -C "$ROOT/kr232" fetch origin -q && git -C "$ROOT/kr232" checkout -B main origin/main -q
git -C "$ROOT/krcompiler" fetch origin -q && git -C "$ROOT/krcompiler" checkout -B master origin/master -q
git -C "$ROOT/krdbg" fetch origin -q && git -C "$ROOT/krdbg" checkout -B main origin/main -q

KRC="$ROOT/krcompiler/build/linux-clang-debug/host/bin/Debug"
cmake --build "$ROOT/krcompiler/build/linux-clang-debug" -j 4 >/tmp/krc-build.log 2>&1 \
    || { tail -15 /tmp/krc-build.log; exit 1; }

cmake -S "$ROOT/kr232" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DKR232_BUILD_KERNEL=ON -DKR232_KRC="$KRC/krc" -DKR232_KRLD="$KRC/krld" >/tmp/kr232-config.log 2>&1 \
    || { tail -15 /tmp/kr232-config.log; exit 1; }
cmake --build "$BUILD" -j 4 >/tmp/kr232-build.log 2>&1 \
    || { tail -15 /tmp/kr232-build.log; exit 1; }

# CMakeLists.txt fails the configure when KR232_BUILD_KERNEL is on and the compiler
# is missing, so the rows that need one cannot vanish silently. This counts them
# anyway: a row that stops being registered for any other reason is exactly the
# failure this gate exists to catch, and ctest reports success either way.
ROWS=$(ctest --test-dir "$BUILD" -N 2>/dev/null | grep -cE '^ *Test +#' || true)
if [ "$ROWS" -ne 15 ]; then
    echo "kr232 registered $ROWS rows, expected 15; a missing row reports success"
    ctest --test-dir "$BUILD" -N 2>&1 | grep -E 'Test +#' || true
    exit 1
fi
echo "kr232 rows registered: $ROWS"

cmake -S "$ROOT/krcompiler" -B "$ROOT/krcompiler/build/linux-clang-debug" \
    -DKRC_KR232_VM="$BUILD/kr232-emu" >/dev/null 2>&1

# Each suite reports and a failure is remembered: passing ctest output through
# grep would replace ctest's status with grep's, which is how a failing suite
# printed PASS once.
failed=0
report() {
    # A suite that registered nothing prints neither a failure nor a count worth
    # trusting, so it is named rather than read as a pass.
    if grep -qE "No tests were found|out of 0" "$1"; then
        echo "   the suite ran no tests"
        cat "$1"
        failed=1
        return
    fi
    if grep -qE "100% tests passed" "$1"; then
        grep -E "tests passed" "$1"
    else
        cat "$1"
        failed=1
    fi
}

echo "== kr232 (kernel rows)"
ctest --test-dir "$BUILD" --output-on-failure >/tmp/kr232-rows.log 2>&1 || true
report /tmp/kr232-rows.log

echo "== krcompiler (whole suite)"
ctest --test-dir "$ROOT/krcompiler/build/linux-clang-debug" >/tmp/krc-rows.log 2>&1 || true
report /tmp/krc-rows.log

echo "== krc_tests"
"$ROOT/krcompiler/build/linux-clang-debug/host/bin/Debug/krc_tests" >/tmp/krc-tests.log 2>&1 || failed=1
grep -E "assertions:" /tmp/krc-tests.log

echo "== krdbg gate"
bash "$ROOT/krdbg/scripts/gate-linux.sh" --arch arm64 >/tmp/krdbg-gate.log 2>&1 && grep -E "GATE LINUX" /tmp/krdbg-gate.log || { tail -20 /tmp/krdbg-gate.log; failed=1; }

echo "== krdbg fixture drift"
cmake -S "$ROOT/krdbg" -B "$ROOT/krdbg/build/fixtures" \
    -DKRDBG_KR232_KRC="$KRC/krc" -DKRDBG_KR232_KRLD="$KRC/krld" \
    -DKRDBG_KRCOMPILER_ROOT="$ROOT/krcompiler" -DKRDBG_KR232_ROOT="$ROOT/kr232" >/dev/null 2>&1
ctest --test-dir "$ROOT/krdbg/build/fixtures" -R fixtures >/tmp/krdbg-fixtures.log 2>&1 || true
report /tmp/krdbg-fixtures.log

if [ "$failed" -ne 0 ]; then
    echo "ARM64 CHECKS: FAIL"
    exit 1
fi
echo "ARM64 CHECKS: PASS"
'@

$script = $remote.Replace('__ROOT__', $Root.Replace('~', '$HOME'))
# Sent as a file rather than an argument: a remote shell re-parses an argument
# and a parenthesis in an echo breaks it.
$local = Join-Path ([System.IO.Path]::GetTempPath()) 'kr232-arm64-check.sh'
[System.IO.File]::WriteAllText($local, $script.Replace("`r`n", "`n"))
scp $local "${Target}:/tmp/kr232-arm64-check.sh" | Out-Null
$output = ssh $Target 'bash /tmp/kr232-arm64-check.sh' 2>&1
$output | ForEach-Object { Write-Output $_ }
if (-not ($output -match 'ARM64 CHECKS: PASS')) {
    throw "the ARM64 checks did not finish: $($output | Select-Object -Last 5)"
}
