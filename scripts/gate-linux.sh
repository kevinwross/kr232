#!/usr/bin/env bash
# Runs the kr232 gate on a Linux host: configure, build, test, spec lint.
#
#   scripts/gate-linux.sh [preset] [jobs]
#
# The preset selects the host row: linux-wsl on CROSBY's WSL (build from the
# ext4 mirror, kr232-impl.md §1), linux-arm64 on BitWhiz. Jobs default to the
# preset's own value, which is 10 on x64 and 4 on the Pi.
set -euo pipefail

preset="${1:-linux-wsl}"
cd "$(dirname "$0")/.."

case "$preset" in
    linux-wsl | linux-arm64) ;;
    *)
        echo "gate-linux: unknown preset '$preset' (linux-wsl or linux-arm64)" >&2
        exit 2
        ;;
esac

echo "kr232 gate: $preset"
cmake --preset "$preset"
cmake --build --preset "$preset"
ctest --preset "$preset"
node tools/check-isa.mjs
echo "kr232 gate: $preset passed"
