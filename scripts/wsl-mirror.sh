#!/usr/bin/env bash
# Refreshes a native-ext4 mirror of the Windows checkout, for Linux builds
# (kr232-impl.md §1).
#
#   scripts/wsl-mirror.sh [source] [mirror]
#
# The mirror is disposable and is never edited or committed: it exists because a
# WSL build on /mnt/c carries host paths in its debug data and its timestamps are
# too coarse for an incremental build. Build in the mirror, edit on Windows.
set -euo pipefail

source_root="${1:-/mnt/c/Users/kevin/repos/kr232}"
mirror="${2:-$HOME/mirrors/kr232}"

case "$source_root" in
    /mnt/c/*) ;;
    *)
        echo "wsl-mirror: source must be under /mnt/c; got '$source_root'" >&2
        exit 2
        ;;
esac

if [ ! -d "$source_root" ]; then
    echo "wsl-mirror: no such directory: $source_root" >&2
    exit 2
fi

mkdir -p "$mirror"
rsync -a --delete --exclude build/ "$source_root"/ "$mirror"/
echo "wsl-mirror: $source_root -> $mirror"
