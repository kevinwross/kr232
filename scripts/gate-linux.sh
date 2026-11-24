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
cmake --preset "$preset"
cmake --build --preset "$preset"
ctest --preset "$preset"
node tools/check-isa.mjs
echo "kr232 gate: $preset passed"
