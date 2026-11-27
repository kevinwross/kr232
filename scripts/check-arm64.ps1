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
set -eu
ROOT="__ROOT__"
BUILD="$ROOT/kr232/build/arm64"

# Fast-forward only: a checkout with local work stops the run instead of having its
# branch rewritten under it, which is what `checkout -B` did.
pull() {
    git -C "$1" fetch origin -q
    git -C "$1" merge --ff-only -q "origin/$2"
}
pull "$ROOT/kr232" main
pull "$ROOT/krcompiler" master
pull "$ROOT/krdbg" main
# krdbg's two-adapter step builds kr216 as a subdirectory, so that checkout has to
# be current too: a stale one lacks the kr216::core target the adapter links, and
# the failure reads as a missing target rather than a stale tree.
pull "$ROOT/kr216" main

# A clean host has no configured tree, so configure before building: the preset
# owns the generator and the Compiler choice.
(cd "$ROOT/krcompiler" && cmake --preset linux-clang-debug) >/tmp/krc-config.log 2>&1 \
    || { tail -15 /tmp/krc-config.log; exit 1; }
KRC="$ROOT/krcompiler/build/linux-clang-debug/host/bin/Debug"
cmake --build "$ROOT/krcompiler/build/linux-clang-debug" -j "$(nproc)" >/tmp/krc-build.log 2>&1 \
    || { tail -15 /tmp/krc-build.log; exit 1; }

# The debugger consumes the compiler as a package, so the tree this run just
# built is installed into one prefix and every later configure is pointed at it.
# Without that step the run tested whatever package the host happened to have, or
# failed with "Could not find krc-debug" and no way to see which tree it wanted.
cmake --install "$ROOT/krcompiler/build/linux-clang-debug" --prefix "$HOME/krc-debug" \
    >/tmp/krc-install.log 2>&1 || { tail -15 /tmp/krc-install.log; exit 1; }
export CMAKE_PREFIX_PATH="$HOME/krc-debug${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
# The krdbg gate builds kr232 as a subproject, and that needs krc and krld on
# PATH to build the guest fixtures; the install just put them here.
export PATH="$HOME/krc-debug/bin:$PATH"

# `--fresh` discards the cache: a directory reused between runs keeps any setting a
# previous run put there, and a stale KR232_WARNINGS_AS_ERRORS=OFF would relax the
# build without saying so.
cmake --fresh -S "$ROOT/kr232" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DKR232_BUILD_KERNEL=ON -DKR232_KRC="$KRC/krc" -DKR232_KRLD="$KRC/krld" >/tmp/kr232-config.log 2>&1 \
    || { tail -15 /tmp/kr232-config.log; exit 1; }
cmake --build "$BUILD" -j "$(nproc)" >/tmp/kr232-build.log 2>&1 \
    || { tail -15 /tmp/kr232-build.log; exit 1; }

# CMakeLists.txt fails the configure when KR232_BUILD_KERNEL is on and the compiler
# is missing, so the rows that need one cannot vanish silently. This counts them
# anyway: a row that stops being registered for any other reason is exactly the
# failure this gate exists to catch, and ctest reports success either way.
ROWS=$(ctest --test-dir "$BUILD" -N 2>/dev/null | grep -cE '^ *Test +#' || true)
if [ "$ROWS" -ne 13 ]; then
    echo "kr232 registered $ROWS rows, expected 13; a missing row reports success"
    ctest --test-dir "$BUILD" -N 2>&1 | grep -E 'Test +#' || true
    exit 1
fi
echo "kr232 rows registered: $ROWS"

# The compiler's run rows execute guest images in a kr32 emulator, and they were
# executing the copied binary in the compiler tree -- older than the image names,
# so it loaded nothing and every run row reported "expected exit 42, got 0". Build
# kr32 from its own current source and point the rows at that binary.
pull "$ROOT/kr32" main
KR32_BUILD="$ROOT/kr32/build/arm64-gate"
cmake --fresh -S "$ROOT/kr32" -B "$KR32_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug >/tmp/kr32-config.log 2>&1 \
    || { tail -15 /tmp/kr32-config.log; exit 1; }
cmake --build "$KR32_BUILD" -j "$(nproc)" >/tmp/kr32-build.log 2>&1 \
    || { tail -15 /tmp/kr32-build.log; exit 1; }
[ -x "$KR32_BUILD/kr32" ] || { echo "kr32 was not built at $KR32_BUILD/kr32"; exit 1; }

cmake -S "$ROOT/krcompiler" -B "$ROOT/krcompiler/build/linux-clang-debug" \
    -DKRC_KR232_VM="$BUILD/kr232" -DKRC_KR32_VM="$KR32_BUILD/kr32" >/dev/null 2>&1

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
    # A disabled test is a hole, not a pass: ctest prints "***Not Run (Disabled)"
    # beside "100% tests passed" and exits 0.
    if grep -qE "Not Run|Disabled" "$1"; then
        echo "   tests were not run"
        grep -E "Not Run|Disabled" "$1"
        failed=1
        return
    fi
    if grep -qE "100% tests passed" "$1"; then
        grep -E "tests passed" "$1"
        skipped=$(grep -cE "Skipped" "$1" || true)
        if [ "$skipped" -gt 0 ]; then
            echo "   note: $skipped test(s) skipped, named in the log"
        fi
    else
        cat "$1"
        failed=1
    fi
}

echo "== kr232 (kernel rows)"
ctest --test-dir "$BUILD" --output-on-failure --no-tests=error >/tmp/kr232-rows.log 2>&1 || true
report /tmp/kr232-rows.log

echo "== krcompiler (whole suite)"
ctest --test-dir "$ROOT/krcompiler/build/linux-clang-debug" --no-tests=error \
    >/tmp/krc-rows.log 2>&1 || true
report /tmp/krc-rows.log

echo "== krc_tests"
"$ROOT/krcompiler/build/linux-clang-debug/host/bin/Debug/krc_tests" >/tmp/krc-tests.log 2>&1 || failed=1
grep -E "assertions:" /tmp/krc-tests.log

echo "== krdbg gate"
bash "$ROOT/krdbg/scripts/gate-linux.sh" --arch arm64 >/tmp/krdbg-gate.log 2>&1 && grep -E "GATE LINUX" /tmp/krdbg-gate.log || { tail -20 /tmp/krdbg-gate.log; failed=1; }

echo "== krdbg fixture drift"
cmake --fresh -S "$ROOT/krdbg" -B "$ROOT/krdbg/build/fixtures" \
    -DKRDBG_KR232_KRC="$KRC/krc" -DKRDBG_KR232_KRLD="$KRC/krld" \
    -DKRDBG_KRCOMPILER_ROOT="$ROOT/krcompiler" -DKRDBG_KR232_ROOT="$ROOT/kr232" >/dev/null 2>&1
# The row is asserted by name, not by count: this project registers other tests
# too, and counting them made a healthy tree fail the gate.
if ! ctest --test-dir "$ROOT/krdbg/build/fixtures" -N 2>/dev/null | grep -q krdbg_kr232_fixtures_match_source; then
    echo "the krdbg fixture-drift row is not registered"
    ctest --test-dir "$ROOT/krdbg/build/fixtures" -N 2>&1 | grep -E 'Test +#' || true
    exit 1
fi
ctest --test-dir "$ROOT/krdbg/build/fixtures" -R fixtures --no-tests=error \
    >/tmp/krdbg-fixtures.log 2>&1 || true
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
