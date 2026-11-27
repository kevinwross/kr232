#!/usr/bin/env bash
# Runs the kr232 gate on a Linux host: configure, build, test, spec lint.
#
#   scripts/gate-linux.sh [preset] [jobs]
#
# The preset selects the host row: linux-wsl on CROSBY's WSL (build from the
# ext4 mirror, kr232-impl.md §1), linux-arm64 on BitWhiz. Jobs default to the
# preset's own value, which is 10 on x64 and 4 on the Pi.
set -euo pipefail

preset="${1:-linux-gcc-debug}"
cd "$(dirname "$0")/.."

# J5: every gate asks the doctor first, so a missing tool fails before the first
# compile with a sentence naming the tool and the repository.
doctor_tool="${KRC_DOCTOR_TOOL:-$(pwd)/../krcompiler/scripts/doctor.sh}"
if [ -f "$doctor_tool" ]; then
    sh "$doctor_tool" kr232 --need=krc,krld
else
    echo "gate: no doctor at '$doctor_tool' (set KRC_DOCTOR_TOOL); continuing without one" >&2
fi

# I2: a build under /mnt/c carries mount paths in its debug data and its
# timestamps are too coarse for an incremental build, so the gate refreshes a
# native mirror through the family tool and re-executes there. --in-place opts
# out and says so, because that is the only case where a mount build is
# intentional. The mirror is native, so the re-executed gate does not loop.
in_place=0
for argument in "$@"; do
    if [ "$argument" = "--in-place" ]; then in_place=1; fi
done
root="$(pwd)"
case "$root" in
    /mnt/*)
        if [ "$in_place" = "1" ]; then
            echo "gate-linux: building on $root (--in-place): debug paths will carry the mount" >&2
        else
            repo="$(basename "$root")"
            mirror_tool="${KRC_MIRROR_TOOL:-$root/../krcompiler/scripts/kr-mirror.sh}"
            if [ ! -f "$mirror_tool" ]; then
                echo "gate-linux: $root is a /mnt path and no mirror tool was found at '$mirror_tool'; set KRC_MIRROR_TOOL or pass --in-place" >&2
                exit 2
            fi
            bash "$mirror_tool" "$repo" --wait 600 >&2
            mirror="$HOME/mirrors/$repo"
            echo "gate-linux: source was $root; building from the mirror at $mirror" >&2
            exec bash "$mirror/scripts/gate-linux.sh" "$@"
        fi
        ;;
esac

case "$preset" in
    linux-gcc-debug | linux-gcc-release | linux-arm64-release) ;;
    *)
        echo "gate-linux: unknown preset '$preset'" >&2
        exit 2
        ;;
esac

echo "kr232 gate: $preset"
# The kernel and the test fixtures are guest code built by krc and krld. Without
# them the kernel and CLI rows have nothing to read, so name the missing tool
# instead of failing on a file.
krc_bin="${KR232_KRC:-$(command -v krc 2>/dev/null || true)}"
krld_bin="${KR232_KRLD:-$(command -v krld 2>/dev/null || true)}"
if [ -z "$krc_bin" ] || [ -z "$krld_bin" ]; then
    echo "kr232 gate: krc and krld are required to build the kernel and the fixtures" >&2
    exit 2
fi
cmake --preset "$preset" -DKR232_BUILD_KERNEL=ON \
    "-DKR232_KRC=$krc_bin" "-DKR232_KRLD=$krld_bin"
# A share handed down by the family gate wins; alone this takes every core. The
# suite ran serially before, which was the longest step in the Linux wave.
# GCC 15 in this WSL image intermittently segfaults while garbage-collecting, so
# the x86_64 rows stay at eight jobs; the ARM64 host uses every processor.
if [ "$(uname -m 2>/dev/null || echo unknown)" = "x86_64" ]; then
    JOBS="${FAMILY_JOBS:-8}"
    [ "$JOBS" -gt 8 ] && JOBS=8
else
    JOBS="${FAMILY_JOBS:-$(nproc 2>/dev/null || echo 4)}"
fi
[ "$JOBS" -lt 1 ] && JOBS=1
cmake --build --preset "$preset" -j "$JOBS"
ctest --preset "$preset" -j "$JOBS"
# The ISA document check needs Node, which this WSL image does not install. Name
# the skip rather than letting the row fail on a missing interpreter.
if command -v node >/dev/null 2>&1; then
    node tools/check-isa.mjs
else
    echo "kr232 gate: node is not installed; skipping the ISA document check" >&2
fi
echo "kr232 gate: $preset passed"
